// LLVM IR for operands and rvalues.

#include "codegen/llvm_internal.h"
#include "sema/const_eval.h"

#include <inttypes.h>
#include <string.h>

static const uint32_t INT_BITS[] = {8, 16, 32, 64, 8, 16, 32, 64};

static const Ty *ty_of(Gen *g, TypeId type) {
    return type_get(g->types, type);
}

static bool is_signed(const Ty *ty) {
    return ty->kind == TY_INT && ty->data <= INT_I64;
}

// LLVM integers are sign-agnostic: write the bit pattern as a signed number
// of the type's width.
static void int_text(char *out, size_t size, uint64_t value, uint32_t bits) {
    uint64_t mask = bits == 64 ? UINT64_MAX : (1ull << bits) - 1;
    value &= mask;
    if (bits < 64 && (value >> (bits - 1)) & 1) {
        value |= ~mask;   // sign-extend
    }
    fmt_into(out, size, "%" PRId64, (int64_t)value);
}

// Floats are written as the hexadecimal bits of a double, the only exact form
// LLVM accepts; an f32 constant is the double of the rounded float.
static void float_text(char *out, size_t size, double value, bool is_f32) {
    double exact = is_f32 ? (double)(float)value : value;
    uint64_t bits;
    memcpy(&bits, &exact, sizeof bits);
    fmt_into(out, size, "0x%016" PRIX64, bits);
}

// The symbol of the mutable global `id`: `@"ofir.g.<module>.<name>"`.
static void global_name(Gen *g, uint32_t id, char *out, size_t size) {
    const Def *def = sema_def(g->sema, (DefId){id});
    StrView name = def_name(g, (DefId){id});
    Out text = {0};
    out_to_memory(&text);
    out_str(&text, "@\"ofir.g.");
    if (def->module != 0) {
        StrView module = g->sema->program.items[def->module].name;
        out_write(&text, module.ptr, module.len);
        out_char(&text, '.');
    }
    out_write(&text, name.ptr, name.len);
    out_char(&text, '"');
    size_t n = text.len < size - 1 ? text.len : size - 1;
    memcpy(out, text.buf, n);
    out[n] = '\0';
    out_free(&text);
}

// The discriminants of the niche enum `g->niche_tag`: the variant without
// a payload, then the one with it.
static void niche_discriminants(Gen *g, int64_t *none, int64_t *some) {
    int v = niche_variant(g, g->niche_tag);
    const Def *def = sema_def(g->sema, (DefId){type_get(g->types, g->niche_tag)->data});
    *none = def->variants->variants[1 - v].discriminant;
    *some = def->variants->variants[v].discriminant;
}

void niche_tag_value(Gen *g, const char *at, Value *v) {
    int64_t none, some;
    niche_discriminants(g, &none, &some);
    char word[16], is_null[16];
    next_temp(g, word);
    out_fmt(&g->out, "  %s = load ptr, ptr %s\n", word, at);
    next_temp(g, is_null);
    out_fmt(&g->out, "  %s = icmp eq ptr %s, null\n", is_null, word);
    next_temp(g, v->text);
    out_fmt(&g->out, "  %s = select i1 %s, i32 %" PRId64 ", i32 %" PRId64 "\n", v->text,
            is_null, none, some);
    v->type = "i32";
}

// A null pointer for the variant without a payload; the other variant's
// payload is already there.
void niche_tag_store(Gen *g, const char *at, Value v) {
    int64_t none, some;
    niche_discriminants(g, &none, &some);
    char text[32];
    fmt_into(text, sizeof text, "%" PRId64, none);
    if (strcmp(v.text, text) == 0) {
        out_fmt(&g->out, "  store ptr null, ptr %s\n", at);
        return;
    }
    fmt_into(text, sizeof text, "%" PRId64, some);
    if (strcmp(v.text, text) == 0) {
        return;
    }
    char word[16], is_none[16], kept[16];   // not a constant
    next_temp(g, word);
    out_fmt(&g->out, "  %s = load ptr, ptr %s\n", word, at);
    next_temp(g, is_none);
    out_fmt(&g->out, "  %s = icmp eq i32 %s, %" PRId64 "\n", is_none, v.text, none);
    next_temp(g, kept);
    out_fmt(&g->out, "  %s = select i1 %s, ptr null, ptr %s\n", kept, is_none, word);
    out_fmt(&g->out, "  store ptr %s, ptr %s\n", kept, at);
}

Value gen_operand(Gen *g, Operand o) {
    Value v = {.type = llvm_type(g, o.type)};
    switch (o.kind) {
    case OPERAND_COPY: {
        if (strcmp(v.type, "void") == 0) {
            break;   // a local of type (): nothing to load
        }
        char at[16];
        place_address(g, o.as.local, o.path, at);
        if (g->has_niche_tag) {   // the tag of a niche enum: whether the pointer is null
            niche_tag_value(g, at, &v);
            break;
        }
        next_temp(g, v.text);
        out_fmt(&g->out, "  %s = load %s, ptr %s\n", v.text, v.type, at);
        break;
    }
    case OPERAND_ADDR:
        v.type = "ptr";
        place_address(g, o.as.local, o.path, v.text);
        break;
    case OPERAND_INT:
        int_text(v.text, sizeof v.text, o.as.int_value, INT_BITS[ty_of(g, o.type)->data]);
        break;
    case OPERAND_FLOAT:
        float_text(v.text, sizeof v.text, o.as.float_value, ty_of(g, o.type)->data == 32);
        break;
    case OPERAND_BOOL: strcpy(v.text, o.as.bool_value ? "true" : "false"); break;
    case OPERAND_STR: {
        uint32_t n = string_global(g, o.as.str);
        fmt_into(v.text, sizeof v.text, "{ ptr @.str.%u, i64 %zu, i64 0 }", n, o.as.str.len);
        break;
    }
    case OPERAND_UNIT: v.type = "void"; break;
    }
    return v;
}

static const char *int_op(Op op, bool is_signed_type) {
    switch (op) {
    case OP_ADD: return "add";
    case OP_SUB: return "sub";
    case OP_MUL: return "mul";
    case OP_DIV: return is_signed_type ? "sdiv" : "udiv";
    case OP_REM: return is_signed_type ? "srem" : "urem";
    case OP_AND: return "and";
    case OP_OR: return "or";
    case OP_XOR: return "xor";
    case OP_SHL: return "shl";
    case OP_SHR: return is_signed_type ? "ashr" : "lshr";
    case OP_EQ: return "icmp eq";
    case OP_NE: return "icmp ne";
    case OP_LT: return is_signed_type ? "icmp slt" : "icmp ult";
    case OP_LE: return is_signed_type ? "icmp sle" : "icmp ule";
    case OP_GT: return is_signed_type ? "icmp sgt" : "icmp ugt";
    case OP_GE: return is_signed_type ? "icmp sge" : "icmp uge";
    default: return "add";
    }
}

static const char *float_op(Op op) {
    switch (op) {
    case OP_ADD: return "fadd";
    case OP_SUB: return "fsub";
    case OP_MUL: return "fmul";
    case OP_DIV: return "fdiv";
    case OP_REM: return "frem";
    case OP_EQ: return "fcmp oeq";
    case OP_NE: return "fcmp une";   // NaN != NaN is true
    case OP_LT: return "fcmp olt";
    case OP_LE: return "fcmp ole";
    case OP_GT: return "fcmp ogt";
    case OP_GE: return "fcmp oge";
    default: return "fadd";
    }
}

static void binary(Gen *g, const Rvalue *rv, Value *result) {
    Value a = gen_operand(g, rv->a), b = gen_operand(g, rv->b);
    const Ty *ty = ty_of(g, rv->a.type);
    const char *op = ty->kind == TY_FLOAT ? float_op(rv->op) : int_op(rv->op, is_signed(ty));
    next_temp(g, result->text);
    out_fmt(&g->out, "  %s = %s %s %s, %s\n", result->text, op, a.type, a.text, b.text);
}

static void checked(Gen *g, const Rvalue *rv, Value *result) {
    Value a = gen_operand(g, rv->a), b = gen_operand(g, rv->b);
    const Ty *ty = ty_of(g, rv->a.type);
    int op = rv->op == OP_ADD ? 0 : rv->op == OP_SUB ? 1 : 2;
    int width = ty->data % 4;   // i8/u8 -> 0, ..., i64/u64 -> 3
    g->overflow_used[is_signed(ty)][op][width] = 1;
    static const char *const OPS[] = {"add", "sub", "mul"};
    char pair[16], flag[16];
    next_temp(g, pair);
    out_fmt(&g->out, "  %s = call { %s, i1 } @llvm.%c%s.with.overflow.%s(%s %s, %s %s)\n", pair,
            a.type, is_signed(ty) ? 's' : 'u', OPS[op], a.type, a.type, a.text, a.type, b.text);
    next_temp(g, result->text);
    out_fmt(&g->out, "  %s = extractvalue { %s, i1 } %s, 0\n", result->text, a.type, pair);
    next_temp(g, flag);
    out_fmt(&g->out, "  %s = extractvalue { %s, i1 } %s, 1\n", flag, a.type, pair);
    out_fmt(&g->out, "  store i1 %s, ptr %%l%u\n", flag, rv->flag.id);
}

static void unary(Gen *g, const Rvalue *rv, Value *result) {
    Value a = gen_operand(g, rv->a);
    next_temp(g, result->text);
    if (rv->op == OP_NEG) {
        out_fmt(&g->out, "  %s = fneg %s %s\n", result->text, a.type, a.text);
    } else {
        out_fmt(&g->out, "  %s = xor %s %s, -1\n", result->text, a.type, a.text);   // also i1
    }
}

static const char *cast_op(const Ty *from, const Ty *to) {
    bool from_ptr = from->kind == TY_PTR || from->kind == TY_REF;
    bool to_ptr = to->kind == TY_PTR || to->kind == TY_REF;
    if (from_ptr || to_ptr) {   // a reference is a pointer
        return from_ptr == to_ptr ? NULL : from_ptr ? "ptrtoint" : "inttoptr";
    }
    if (from->kind == TY_FLOAT && to->kind == TY_FLOAT) {
        return from->data < to->data ? "fpext" : "fptrunc";
    }
    if (from->kind == TY_FLOAT) {
        return is_signed(to) ? "fptosi" : "fptoui";
    }
    if (to->kind == TY_FLOAT) {
        return is_signed(from) ? "sitofp" : "uitofp";
    }
    uint32_t from_bits = INT_BITS[from->data], to_bits = INT_BITS[to->data];
    if (from_bits == to_bits) {
        return NULL;   // same bits, different signedness
    }
    return to_bits < from_bits ? "trunc" : is_signed(from) ? "sext" : "zext";
}

void gen_cast_value(Gen *g, TypeId from, TypeId to, Value a, Value *result) {
    const char *op = cast_op(ty_of(g, from), ty_of(g, to));
    if (!op) {
        *result = a;
        result->type = llvm_type(g, to);
        return;
    }
    next_temp(g, result->text);
    out_fmt(&g->out, "  %s = %s %s %s to %s\n", result->text, op, a.type, a.text,
            llvm_type(g, to));
}

static void cast(Gen *g, const Rvalue *rv, Value *result) {
    gen_cast_value(g, rv->a.type, rv->to, gen_operand(g, rv->a), result);
}

void gen_rvalue(Gen *g, const Rvalue *rv, TypeId dest_type, Value *result) {
    result->type = llvm_type(g, dest_type);
    switch (rv->kind) {
    case RV_USE: {
        Value v = gen_operand(g, rv->a);
        memcpy(result->text, v.text, sizeof v.text);
        break;
    }
    case RV_BINARY: binary(g, rv, result); break;
    case RV_CHECKED: checked(g, rv, result); break;
    case RV_UNARY: unary(g, rv, result); break;
    case RV_CAST: cast(g, rv, result); break;
    case RV_CAST_FAILS: gen_cast_fails(g, rv, result); break;
    case RV_SATURATE: gen_saturate(g, rv, result); break;
    case RV_AGGREGATE: gen_aggregate(g, rv, dest_type, result); break;
    case RV_STR_LEN: case RV_STR_BYTE: case RV_FLOAT_FN:
    case RV_PTR_ADD: case RV_SIZE_OF: case RV_ALIGN_OF:
        gen_misc(g, rv, result);
        break;
    case RV_GLOBAL_ADDR:
        while (g->globals_used.len <= rv->def) {
            VEC_PUSH(g->globals_used, false);
        }
        g->globals_used.data[rv->def] = true;
        global_name(g, rv->def, result->text, sizeof result->text);
        break;
    case RV_ATOMIC: gen_atomic(g, rv, result); break;
    }
}

// The definitions of the mutable globals used, with their initial values.
void gen_mut_globals(Gen *g) {
    for (uint32_t id = 0; id < g->globals_used.len; id++) {
        if (!g->globals_used.data[id]) {
            continue;
        }
        const Def *def = sema_def(g->sema, (DefId){id});
        char name[160], init[64] = "zeroinitializer";
        global_name(g, id, name, sizeof name);
        const struct ConstValue *v = def->value;
        if (v && v->kind == CV_INT) {
            int_text(init, sizeof init, v->bits, INT_BITS[ty_of(g, def->type)->data]);
        } else if (v && v->kind == CV_FLOAT) {
            float_text(init, sizeof init, v->f, ty_of(g, def->type)->data == 32);
        } else if (v && v->kind == CV_BOOL) {
            fmt_into(init, sizeof init, "%s", v->b ? "true" : "false");
        }
        bool tls = find_attribute(g->sema, def->item->attributes, "threadLocal");
        out_fmt(&g->out, "\n%s = internal %sglobal %s %s\n", name, tls ? "thread_local " : "",
                llvm_type(g, def->type), init);
    }
}
