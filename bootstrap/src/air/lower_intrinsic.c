// Lowering of the intrinsics that std declares in `extern "intrinsic"`
// blocks (ADR 0019): `wrapping_add_i64(a, b)`, `sqrt_f64(x)`, ... The name
// gives the operation; the signature gives the types.

#include "air/lower_internal.h"

#include <string.h>

typedef struct {
    const char *prefix;   // the name without its type suffix
    RvalueKind kind;
    Op op;
    FloatFn fn;
} Intrinsic;

static const Intrinsic INTRINSICS[] = {
    {"wrapping_add_", RV_BINARY, OP_ADD, 0},
    {"wrapping_sub_", RV_BINARY, OP_SUB, 0},
    {"wrapping_mul_", RV_BINARY, OP_MUL, 0},
    {"sqrt_", RV_FLOAT_FN, 0, FF_SQRT},
    {"fabs_", RV_FLOAT_FN, 0, FF_FABS},
    {"floor_", RV_FLOAT_FN, 0, FF_FLOOR},
    {"ceil_", RV_FLOAT_FN, 0, FF_CEIL},
    {"minnum_", RV_FLOAT_FN, 0, FF_MINNUM},
    {"maxnum_", RV_FLOAT_FN, 0, FF_MAXNUM},
};

static const Intrinsic *find(StrView name) {
    for (size_t i = 0; i < ARRAY_LEN(INTRINSICS); i++) {
        size_t len = strlen(INTRINSICS[i].prefix);
        if (name.len > len && memcmp(name.ptr, INTRINSICS[i].prefix, len) == 0) {
            return &INTRINSICS[i];
        }
    }
    return NULL;
}

// `sizeOf<T>()` and `alignOf<T>()`: the type argument is substituted with
// the instance's.
static Operand size_of(Lowerer *l, Expr *call, RvalueKind kind) {
    TypeId type = type_get(l->types, call->as.call.type_args)->args[0];
    Rvalue rv = {.kind = kind, .to = type};
    return op_copy(emit_temp(l, type_id(TID_INT), rv), type_id(TID_INT));
}

// The atomic intrinsics of std.mem.atomic (ADR 0042), in AtomicOp order,
// with the orderings each one allows.
#define ORDER_BIT(o) (1u << (o))
#define ALL_ORDERS 0x1Fu

static const struct {
    const char *name;
    unsigned allowed;
} ATOMICS[] = {
    {"atomicLoad", ORDER_BIT(ORDER_RELAXED) | ORDER_BIT(ORDER_ACQUIRE) | ORDER_BIT(ORDER_SEQ_CST)},
    {"atomicStore", ORDER_BIT(ORDER_RELAXED) | ORDER_BIT(ORDER_RELEASE) | ORDER_BIT(ORDER_SEQ_CST)},
    {"atomicSwap", ALL_ORDERS},
    {"atomicFetchAdd", ALL_ORDERS},
    {"atomicFetchSub", ALL_ORDERS},
    {"atomicCompareExchange", ALL_ORDERS},
    {"fence", ALL_ORDERS & ~ORDER_BIT(ORDER_RELAXED)},
};

static const char *const ORDER_NAMES[] = {"Relaxed", "Acquire", "Release", "AcqRel", "SeqCst"};

// The variant an ordering argument names, if it is a constant `Ordering.X`.
static bool constant_order(Lowerer *l, const Expr *e, uint32_t *variant) {
    if (e->kind == EXPR_FIELD && e->as.field.res.kind == RES_VARIANT) {
        *variant = e->as.field.res.index;
        return true;
    }
    if (e->kind == EXPR_VARIANT) {   // `.SeqCst`
        *variant = e->as.variant.index;
        return true;
    }
    if (e->kind == EXPR_IDENT && e->as.ident.res.kind == RES_DEF) {
        const Def *def = sema_def(l->sema, (DefId){e->as.ident.res.index});
        if (def->kind == DEF_VARIANT) {
            *variant = def->variant;
            return true;
        }
    }
    return false;
}

// `atomicLoad(p, Ordering.Acquire)`, ...: the ordering is the last argument.
static Operand lower_atomic(Lowerer *l, Expr *call, AtomicOp op) {
    ArgList args = call->as.call.args;
    const Expr *order = args.items[args.len - 1].value;
    uint32_t variant = 0;
    Diagnostics *diags = &l->sema->session->diags;
    if (!constant_order(l, order, &variant)) {
        diag_error(diags, "E0820", order->span,
                   "the memory ordering of an atomic operation must be a constant");
        return op_unit();
    }
    if (!(ATOMICS[op].allowed & ORDER_BIT(variant))) {
        diag_error(diags, "E0821", order->span, "`%s` does not allow the ordering `%s`",
                   ATOMICS[op].name, ORDER_NAMES[variant]);
        return op_unit();
    }
    Rvalue rv = {.kind = RV_ATOMIC, .atomic = op, .order = (AtomicOrder)variant};
    if (op != ATOMIC_FENCE) {
        rv.a = lower_expr(l, args.items[0].value);
    }
    if (args.len > 2) {
        rv.b = lower_expr(l, args.items[1].value);
    }
    if (op == ATOMIC_CAS) {
        rv.items = ARENA_NEW(&l->sema->session->arena, Operand);
        rv.items[0] = lower_expr(l, args.items[2].value);
    }
    LocalId result = emit_temp(l, call->type, rv);
    return call->type.id == TID_UNIT ? op_unit() : op_copy(result, call->type);
}

Operand lower_intrinsic(Lowerer *l, Expr *call, FnDecl *fn) {
    StrView name = symbol_name(&l->sema->session->names, fn->name);
    for (uint32_t i = 0; i < ARRAY_LEN(ATOMICS); i++) {
        if (sv_eq(name, sv_from_cstr(ATOMICS[i].name))) {
            return lower_atomic(l, call, (AtomicOp)i);
        }
    }
    if (sv_eq(name, SV("sizeOf"))) {
        return size_of(l, call, RV_SIZE_OF);
    }
    if (sv_eq(name, SV("alignOf"))) {
        return size_of(l, call, RV_ALIGN_OF);
    }
    if (sv_eq(name, SV("assert"))) {   // std.testing (ADR 0036)
        lower_panic_assert(l, call, BUILTIN_ASSERT);
        return op_unit();
    }
    if (sv_eq(name, SV("dropInPlace")) || sv_eq(name, SV("forget"))) {
        return lower_drop_intrinsic(l, call, sv_eq(name, SV("forget")));
    }
    if (sv_eq(name, SV("cloneOf"))) {
        return lower_clone_of(l, call);
    }
    if (sv_eq(name, SV("hashOf"))) {
        return lower_hash_of(l, call);
    }
    const Intrinsic *op = find(name);
    if (!op) {
        diag_error(&l->sema->session->diags, "E0805", fn->span, "unknown intrinsic");
        return op_unit();
    }
    ArgList args = call->as.call.args;
    Operand a = lower_expr(l, args.items[0].value);
    Operand b = args.len > 1 ? lower_expr(l, args.items[1].value) : op_unit();
    Rvalue rv = {.kind = op->kind, .op = op->op, .fn = op->fn, .a = a, .b = b};
    return op_copy(emit_temp(l, call->type, rv), call->type);
}
