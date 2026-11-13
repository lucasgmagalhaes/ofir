// LLVM IR for select, str length and bytes, and the float functions.

#include "codegen/llvm_internal.h"

#include <string.h>

static const char *const FLOAT_FNS[FF_COUNT] = {
    [FF_SQRT] = "sqrt", [FF_FABS] = "fabs", [FF_FLOOR] = "floor",
    [FF_CEIL] = "ceil", [FF_MINNUM] = "minnum", [FF_MAXNUM] = "maxnum",
};

static void float_fn(Gen *g, const Rvalue *rv, Value *result) {
    bool binary = rv->fn == FF_MINNUM || rv->fn == FF_MAXNUM;
    Value a = gen_operand(g, rv->a);
    Value b = binary ? gen_operand(g, rv->b) : a;   // both loads come before the call
    bool is_f64 = strcmp(a.type, "double") == 0;
    g->float_fn_used[rv->fn][is_f64] = true;
    next_temp(g, result->text);
    out_fmt(&g->out, "  %s = call %s @llvm.%s.%s(%s %s", result->text, a.type, FLOAT_FNS[rv->fn],
            is_f64 ? "f64" : "f32", a.type, a.text);
    if (binary) {
        out_fmt(&g->out, ", %s %s", b.type, b.text);
    }
    out_str(&g->out, ")\n");
}

void gen_float_decls(Gen *g) {
    for (int fn = 0; fn < FF_COUNT; fn++) {
        for (int is_f64 = 0; is_f64 < 2; is_f64++) {
            if (!g->float_fn_used[fn][is_f64]) {
                continue;
            }
            const char *type = is_f64 ? "double" : "float";
            bool binary = fn == FF_MINNUM || fn == FF_MAXNUM;
            out_fmt(&g->out, "declare %s @llvm.%s.%s(%s%s%s)\n", type, FLOAT_FNS[fn],
                    is_f64 ? "f64" : "f32", type, binary ? ", " : "", binary ? type : "");
        }
    }
}

void gen_aggregate(Gen *g, const Rvalue *rv, TypeId type, Value *result) {
    const Ty *ty = type_get(g->types, type);
    bool is_struct = ty->kind == TY_ADT &&
                     sema_def(g->sema, (DefId){ty->data})->kind == DEF_STRUCT;
    const uint32_t *slots = is_struct ? field_slots(g, type) : NULL;
    char current[16] = "poison";
    for (uint32_t i = 0; i < rv->count; i++) {
        Value field = gen_operand(g, rv->items[i]);
        if (strcmp(field.type, "void") == 0) {
            continue;   // a field of type (): nothing to store
        }
        char next[16];
        next_temp(g, next);
        out_fmt(&g->out, "  %s = insertvalue %s %s, %s %s, %u\n", next, result->type, current,
                field.type, field.text, slots ? slots[i] : i);
        memcpy(current, next, sizeof next);
    }
    memcpy(result->text, current, sizeof current);
}

// The address of element 1 from null is the size, in LLVM's own layout.
static void size_of(Gen *g, TypeId type, Value *result) {
    char end[16];
    next_temp(g, end);
    const char *t = llvm_type(g, type);
    t = strcmp(t, "void") ? t : "{}";   // () has no LLVM type of its own
    out_fmt(&g->out, "  %s = getelementptr %s, ptr null, i32 1\n", end, t);
    next_temp(g, result->text);
    out_fmt(&g->out, "  %s = ptrtoint ptr %s to i64\n", result->text, end);
}

// The offset of a `T` after an `i1` is its alignment.
static void align_of(Gen *g, TypeId type, Value *result) {
    char end[16];
    next_temp(g, end);
    const char *t = llvm_type(g, type);
    t = strcmp(t, "void") ? t : "{}";
    out_fmt(&g->out, "  %s = getelementptr { i1, %s }, ptr null, i32 0, i32 1\n", end, t);
    next_temp(g, result->text);
    out_fmt(&g->out, "  %s = ptrtoint ptr %s to i64\n", result->text, end);
}

void gen_misc(Gen *g, const Rvalue *rv, Value *result) {
    switch (rv->kind) {
    case RV_PTR_ADD: {
        Value p = gen_operand(g, rv->a), n = gen_operand(g, rv->b);
        const Ty *ptr = type_get(g->types, rv->a.type);
        const char *elem = llvm_type(g, ptr->inner);
        elem = strcmp(elem, "void") ? elem : "{}";   // () steps by zero bytes
        next_temp(g, result->text);
        out_fmt(&g->out, "  %s = getelementptr %s, ptr %s, i64 %s\n", result->text, elem,
                p.text, n.text);
        break;
    }
    case RV_SIZE_OF: size_of(g, rv->to, result); break;
    case RV_ALIGN_OF: align_of(g, rv->to, result); break;
    case RV_STR_LEN: {
        Value s = gen_operand(g, rv->a);
        next_temp(g, result->text);
        out_fmt(&g->out, "  %s = extractvalue %%ofir.str %s, 1\n", result->text, s.text);
        break;
    }
    case RV_STR_BYTE: {
        Value s = gen_operand(g, rv->a), i = gen_operand(g, rv->b);
        char ptr[16], at[16];
        next_temp(g, ptr);
        out_fmt(&g->out, "  %s = extractvalue %%ofir.str %s, 0\n", ptr, s.text);
        next_temp(g, at);
        out_fmt(&g->out, "  %s = getelementptr inbounds i8, ptr %s, i64 %s\n", at, ptr, i.text);
        next_temp(g, result->text);
        out_fmt(&g->out, "  %s = load i8, ptr %s\n", result->text, at);
        break;
    }
    default: float_fn(g, rv, result); break;
    }
}
