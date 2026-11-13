// Lowering of operators, casts, interpolation and printing. Integer
// arithmetic is checked (spec 2.2): every check is an explicit assertion.

#include "air/lower_internal.h"

static const uint32_t INT_BITS[] = {8, 16, 32, 64, 8, 16, 32, 64};

static bool is_signed(const Ty *ty) {
    return ty->data <= INT_I64;
}

static Operand temp_value(Lowerer *l, TypeId type, Rvalue rvalue) {
    return op_copy(emit_temp(l, type, rvalue), type);
}

static Operand compare(Lowerer *l, Op op, Operand a, Operand b) {
    return temp_value(l, type_id(TID_BOOL), (Rvalue){.kind = RV_BINARY, .op = op, .a = a, .b = b});
}

static Operand checked(Lowerer *l, const Arith *x) {
    LocalId flag = new_temp(l, type_id(TID_BOOL));
    Rvalue rv = {.kind = RV_CHECKED, .op = x->op, .a = x->a, .b = x->b, .flag = flag};
    Operand result = temp_value(l, x->type, rv);
    emit_assert(l, op_copy(flag, type_id(TID_BOOL)), PANIC_OVERFLOW, x->span);
    return result;
}

// Division and remainder panic on a zero divisor and on MIN / -1.
static Operand divide(Lowerer *l, const Arith *x) {
    const Ty *ty = type_get(l->types, x->type);
    emit_assert(l, compare(l, OP_EQ, x->b, op_int(0, x->type)), PANIC_DIV_ZERO, x->span);
    if (is_signed(ty)) {
        uint32_t bits = INT_BITS[ty->data];
        Operand min = op_int(1ull << (bits - 1), x->type);   // the bit pattern of MIN
        Operand a_min = compare(l, OP_EQ, x->a, min);
        Operand b_neg = compare(l, OP_EQ, x->b, op_int(UINT64_MAX >> (64 - bits), x->type));
        Rvalue both = {.kind = RV_BINARY, .op = OP_AND, .a = a_min, .b = b_neg};
        emit_assert(l, temp_value(l, type_id(TID_BOOL), both), PANIC_OVERFLOW, x->span);
    }
    return temp_value(l, x->type, (Rvalue){.kind = RV_BINARY, .op = x->op, .a = x->a, .b = x->b});
}

// A shift by the bit width or more panics; a negative amount, seen as
// unsigned, is out of range too.
static Operand shift(Lowerer *l, const Arith *x) {
    const Ty *ty = type_get(l->types, x->type);
    TypeId u64 = type_id(TID_U64);
    Operand amount = temp_value(l, u64, (Rvalue){.kind = RV_CAST, .a = x->b, .to = u64});
    Operand bits = op_int(INT_BITS[ty->data], u64);
    emit_assert(l, compare(l, OP_GE, amount, bits), PANIC_SHIFT, x->span);
    return temp_value(l, x->type, (Rvalue){.kind = RV_BINARY, .op = x->op, .a = x->a, .b = x->b});
}

static Operand addr(Lowerer *l, Operand value) {
    LocalId local = to_local(l, value);
    return (Operand){.kind = OPERAND_ADDR, .type = value.type, .as.local = local};
}

static Operand str_op(Lowerer *l, Op op, Operand a, Operand b) {
    bool equality = op == OP_EQ || op == OP_NE;
    TypeId result = type_id(op == OP_ADD ? TID_STR : equality ? TID_BOOL : TID_I32);
    LocalId dest = new_temp(l, result);
    Operand *args = ARENA_ARRAY(&l->sema->session->arena, Operand, 2);   // std/core/str.of
    args[0] = a;
    args[1] = b;
    args[0].type = args[1].type = type_id(TID_STR);
    const char *name = op == OP_ADD ? "strConcat" : equality ? "strEq" : "strCmp";
    emit_direct_call(l, std_fn(l, "std.core.str", name), type_id(TID_UNIT),
                     (CallArgs){args, 2}, &dest);
    if (op == OP_ADD) {
        return op_copy(dest, result);
    }
    if (op == OP_EQ) {
        return op_copy(dest, result);
    }
    if (op == OP_NE) {
        return temp_value(l, result, (Rvalue){.kind = RV_UNARY, .op = OP_NOT,
                                              .a = op_copy(dest, result)});
    }
    return compare(l, op, op_copy(dest, result), op_int(0, result));   // cmp(a, b) < 0, ...
}

Operand lower_binary_op(Lowerer *l, Arith x) {
    const Ty *ty = type_get(l->types, x.type);
    if (ty->kind == TY_STR) {
        return str_op(l, x.op, x.a, x.b);
    }
    if (ty->kind == TY_ADT) {   // == and != on enums without payloads
        return enum_equality(l, x.op, x.a, x.b);
    }
    if (x.op >= OP_EQ && x.op <= OP_GE) {
        return compare(l, x.op, x.a, x.b);
    }
    if (ty->kind == TY_INT) {
        switch (x.op) {
        case OP_ADD: case OP_SUB: case OP_MUL: return checked(l, &x);
        case OP_DIV: case OP_REM: return divide(l, &x);
        case OP_SHL: case OP_SHR: return shift(l, &x);
        default: break;
        }
    }
    return temp_value(l, x.type, (Rvalue){.kind = RV_BINARY, .op = x.op, .a = x.a, .b = x.b});
}

// `&s` of a str: a copy with `cap == 0`, a view that is never freed and
// that a modification copies first (check_ref.c).
static Operand str_view(Lowerer *l, Expr *e) {
    Operand value = lower_expr(l, e->as.unary.operand);
    value.type = type_id(TID_STR);
    LocalId view = emit_temp(l, value.type, (Rvalue){.kind = RV_USE, .a = value});
    uint32_t *cap = ARENA_NEW(&l->sema->session->arena, uint32_t);
    *cap = 2;
    emit_store(l, (Place){.local = view, .path = {.fields = cap, .len = 1}},
               op_int(0, type_id(TID_INT)));
    return op_copy(view, value.type);
}

Operand lower_unary(Lowerer *l, Expr *e) {
    if (e->as.unary.op == OP_DEREF) {
        return lower_field_read(l, e);   // a place reached through the pointer
    }
    if (e->as.unary.op == OP_REF || e->as.unary.op == OP_REF_MUT) {
        const Ty *targs = type_get(l->types, l->body->targs);   // `&V` with V = str is a view
        if (type_subst(l->types, e->type, targs->args, targs->arg_count).id == TID_STR) {
            return str_view(l, e);
        }
        return place_operand(lower_place(l, e->as.unary.operand), OPERAND_ADDR, e->type);
    }
    Operand value = lower_expr(l, e->as.unary.operand);
    if (e->as.unary.op == OP_NEG && type_get(l->types, e->type)->kind == TY_INT) {
        if (value.kind == OPERAND_INT) {   // a negative literal, already range checked
            value.as.int_value = 0 - value.as.int_value;
            return value;
        }
        Arith x = {.op = OP_SUB, .type = e->type, .a = op_int(0, e->type), .b = value,
                   .span = e->span};
        return checked(l, &x);
    }
    return temp_value(l, e->type, (Rvalue){.kind = RV_UNARY, .op = e->as.unary.op, .a = value});
}

// Integer conversions that can lose information, and float to integer
// conversions, check that the value fits (spec 2.1).
static bool cast_needs_check(const Ty *from, const Ty *to) {
    if (to->kind != TY_INT || from->kind == TY_PTR) {
        return false;
    }
    if (from->kind == TY_FLOAT) {
        return true;
    }
    if (from->kind != TY_INT || from->data == to->data) {
        return false;
    }
    bool same_sign = is_signed(from) == is_signed(to);
    uint32_t from_bits = INT_BITS[from->data], to_bits = INT_BITS[to->data];
    return same_sign ? to_bits < from_bits : !(is_signed(to) && to_bits > from_bits);
}

Operand lower_cast(Lowerer *l, Operand value, TypeId to, Span span) {
    if (type_eq(value.type, to)) {
        return value;
    }
    if (cast_needs_check(type_get(l->types, value.type), type_get(l->types, to))) {
        Rvalue fails = {.kind = RV_CAST_FAILS, .a = value, .to = to};
        emit_assert(l, temp_value(l, type_id(TID_BOOL), fails), PANIC_CAST, span);
    }
    return temp_value(l, to, (Rvalue){.kind = RV_CAST, .a = value, .to = to});
}

// ---------------------------------------------------------------------------
// Interpolation and printing

// The runtime function for one part, with the part widened to its argument
// type (int64_t, uint64_t, double, float, bool or a str pointer).
static RuntimeFn part_fn(Lowerer *l, Operand *part, bool print) {
    const Ty *ty = type_get(l->types, part->type);
    switch (ty->kind) {
    case TY_INT:
        *part = lower_cast(l, *part, type_id(is_signed(ty) ? TID_I64 : TID_U64), (Span){0});
        return is_signed(ty) ? (print ? RT_PRINT_INT : RT_STR_PUSH_INT)
                             : (print ? RT_PRINT_UINT : RT_STR_PUSH_UINT);
    case TY_FLOAT:
        if (ty->data == 32) {
            return print ? RT_PRINT_F32 : RT_STR_PUSH_F32;
        }
        return print ? RT_PRINT_FLOAT : RT_STR_PUSH_FLOAT;
    case TY_BOOL: return print ? RT_PRINT_BOOL : RT_STR_PUSH_BOOL;
    default:
        *part = addr(l, *part);
        return print ? RT_PRINT_STR : RT_STR_PUSH_STR;
    }
}

// The value of "a {x} b": an empty str that each part is appended to.
Operand lower_interpolation(Lowerer *l, Expr *e) {
    TypeId str = type_id(TID_STR);
    Operand empty = {.kind = OPERAND_STR, .type = str, .as.str = SV("")};
    LocalId dest = emit_temp(l, str, (Rvalue){.kind = RV_USE, .a = empty});
    Operand target = {.kind = OPERAND_ADDR, .type = str, .as.local = dest};
    for (uint32_t i = 0; i < e->as.parts.len; i++) {
        Operand part = lower_expr(l, e->as.parts.items[i]);
        RuntimeFn fn = part_fn(l, &part, false);
        Operand args[] = {target, part};
        emit_runtime(l, fn, args, 2, NULL);
    }
    return op_copy(dest, str);
}

// A part that cannot read the str it is appended to: a literal, or a
// scalar built from scalar locals with operators and casts. Its value is the
// same before and after the parts ahead of it are appended.
static bool scalar_part(Lowerer *l, const Expr *e) {
    TyKind kind = type_get(l->types, e->type)->kind;
    if (e->kind == EXPR_STR) {
        return true;
    }
    if (kind != TY_INT && kind != TY_FLOAT && kind != TY_BOOL) {
        return false;
    }
    switch (e->kind) {
    case EXPR_INT: case EXPR_FLOAT: case EXPR_BYTE: case EXPR_BOOL: return true;
    case EXPR_IDENT: return e->as.ident.res.kind == RES_LOCAL;
    case EXPR_CAST: return scalar_part(l, e->as.cast.value);
    case EXPR_UNARY:
        return (e->as.unary.op == OP_NEG || e->as.unary.op == OP_NOT) &&
               scalar_part(l, e->as.unary.operand);
    case EXPR_BINARY:
        return e->as.binary.op != OP_COALESCE && scalar_part(l, e->as.binary.lhs) &&
               scalar_part(l, e->as.binary.rhs);
    default: return false;
    }
}

// `s.push("a {x} b")` appends each part to `s` directly when no part can
// read `s`, instead of building the string and copying it.
bool lower_push_interpolation(Lowerer *l, Expr *call, const FnDecl *fn) {
    const Def *def = sema_def(l->sema, fn->def);
    Expr *receiver = call->as.call.callee->as.field.base;
    if (call->as.call.args.len != 1 || !module_in_std(l->sema, def->module) ||
        !symbol_eq(def->name, intern(&l->sema->session->names, SV("push"))) ||
        !type_eq(receiver->type, type_id(TID_STR))) {
        return false;
    }
    Expr *arg = call->as.call.args.items[0].value;
    if (arg->kind != EXPR_INTERP) {
        return false;
    }
    for (uint32_t i = 0; i < arg->as.parts.len; i++) {
        if (!scalar_part(l, arg->as.parts.items[i])) {
            return false;
        }
    }
    TypeId str = type_id(TID_STR);
    Operand target = place_operand(lower_place(l, receiver), OPERAND_ADDR, str);
    for (uint32_t i = 0; i < arg->as.parts.len; i++) {
        Operand part = lower_expr(l, arg->as.parts.items[i]);
        RuntimeFn rt = part_fn(l, &part, false);
        Operand args[] = {target, part};
        emit_runtime(l, rt, args, 2, NULL);
    }
    return true;
}

// print("a {x}") writes each part directly, without building the string.
void lower_print(Lowerer *l, Expr *arg, bool newline) {
    bool parts = arg->kind == EXPR_INTERP;
    uint32_t count = parts ? arg->as.parts.len : 1;
    for (uint32_t i = 0; i < count; i++) {
        Operand part = lower_expr(l, parts ? arg->as.parts.items[i] : arg);
        RuntimeFn fn = part_fn(l, &part, true);
        emit_runtime(l, fn, &part, 1, NULL);
    }
    if (newline) {
        emit_runtime(l, RT_PRINT_NEWLINE, NULL, 0, NULL);
    }
}
