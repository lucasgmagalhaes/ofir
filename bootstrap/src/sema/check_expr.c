// Type checking of expressions (spec 2 and 4.8).

#include "sema/check_internal.h"

static StrView def_name(Checker *c, const Def *def) {
    return symbol_name(&c->sema->session->names, def->name);
}

// ---------------------------------------------------------------------------
// Literals

static bool int_fits(const Ty *ty, uint64_t value, bool negative) {
    static const uint64_t MAX[] = {
        [INT_I8] = INT8_MAX, [INT_I16] = INT16_MAX, [INT_I32] = INT32_MAX, [INT_I64] = INT64_MAX,
        [INT_U8] = UINT8_MAX, [INT_U16] = UINT16_MAX, [INT_U32] = UINT32_MAX,
        [INT_U64] = UINT64_MAX,
    };
    if (negative) {   // negating an unsigned value is reported by the `-` operator
        return ty->data > INT_I64 || value <= MAX[ty->data] + 1;
    }
    return value <= MAX[ty->data];
}

// An integer literal takes the expected integer or float type, else `int`.
static TypeId int_literal(Checker *c, Expr *e, TypeId expected, bool negative) {
    const Ty *ty = type_get(c->types, expected);
    TypeId type = ty->kind == TY_INT || ty->kind == TY_FLOAT ? expected : type_id(TID_INT);
    ty = type_get(c->types, type);
    if (ty->kind == TY_INT && !int_fits(ty, e->as.int_value, negative)) {
        check_error(c, "E0312", e->span, "integer literal out of range for `%s`",
                    type_name(c, type));
    }
    return type;
}

static TypeId float_literal(Checker *c, TypeId expected) {
    return type_get(c->types, expected)->kind == TY_FLOAT ? expected : type_id(TID_FLOAT);
}

static TypeId interpolation(Checker *c, Expr *e) {
    for (uint32_t i = 0; i < e->as.parts.len; i++) {
        TypeId type = check_expr(c, e->as.parts.items[i], NO_TYPE);
        type = auto_deref(c, &e->as.parts.items[i], type);   // `"{r}"` shows `*r`
        Expr *part = e->as.parts.items[i];
        TyKind kind = type_get(c->types, type)->kind;
        bool printable = kind == TY_INT || kind == TY_FLOAT || kind == TY_BOOL ||
                         kind == TY_STR || kind == TY_ERROR;
        if (!printable) {
            check_error(c, "E0314", part->span,
                        "a value of type `%s` cannot be interpolated into a string",
                        type_name(c, type));
        }
    }
    return type_id(TID_STR);
}

// ---------------------------------------------------------------------------
// Names

static TypeId def_value(Checker *c, Def *def, Span span) {
    switch (def->kind) {
    case DEF_CONST: return check_const(c, def);
    case DEF_GLOBAL:   // spec 11.5: another thread may write it
        require_unsafe(c, span, "using a mutable global");
        return check_const(c, def);
    case DEF_FN:   // stage 1 has them (spec 10)
        check_error(c, "E0901", span, "function values are not supported by stage 0");
        return type_id(TID_ERROR);
    case DEF_BUILTIN: return unsupported(c, span, "function values are");
    default:
        check_error(c, "E0310", span, "expected a value, found type `" SV_FMT "`",
                    SV_ARG(def_name(c, def)));
        return type_id(TID_ERROR);
    }
}

static TypeId name_value(Checker *c, Expr *e, TypeId expected) {
    Res res = e->as.ident.res;
    switch (res.kind) {
    case RES_LOCAL: return c->local_types[res.index];
    case RES_DEF: {
        Def *def = sema_def(c->sema, (DefId){res.index});
        if (def->kind == DEF_VARIANT) {   // `None`
            return check_prelude_variant(c, e, def, NULL, expected);
        }
        return def_value(c, def, e->span);
    }
    default: return type_id(TID_ERROR);
    }
}

// ---------------------------------------------------------------------------
// Operators

static bool is_literal(const Expr *e) {
    if (e->kind == EXPR_UNARY && e->as.unary.op == OP_NEG) {
        e = e->as.unary.operand;
    }
    return e->kind == EXPR_INT || e->kind == EXPR_FLOAT;
}

static TypeId operator_error(Checker *c, Span span, TypeId type) {
    check_error(c, "E0313", span, "operator cannot be applied to type `%s`", type_name(c, type));
    return type_id(TID_ERROR);
}

static TypeId unary(Checker *c, Expr *e, TypeId expected) {
    Expr *operand = e->as.unary.operand;
    Op op = e->as.unary.op;
    if (op == OP_DEREF) {
        return check_deref(c, e, expected);
    }
    if (op == OP_REF || op == OP_REF_MUT) {
        return check_ref(c, e, expected);
    }
    TypeId type = op == OP_NEG && operand->kind == EXPR_INT
                      ? int_literal(c, operand, expected, true)
                      : check_expr(c, operand, expected);
    operand->type = type;
    const Ty *ty = type_get(c->types, type);
    bool ok = op == OP_NEG ? ty->kind == TY_FLOAT || (ty->kind == TY_INT && ty->data <= INT_I64)
                           : ty->kind == TY_BOOL || ty->kind == TY_INT;
    return ok || ty->kind == TY_ERROR ? type : operator_error(c, e->span, type);
}

// Which operand types an operator accepts.
static bool operand_ok(const Ty *ty, Op op) {
    switch (op) {
    case OP_ADD: return ty->kind == TY_INT || ty->kind == TY_FLOAT || ty->kind == TY_STR;
    case OP_SUB: case OP_MUL: case OP_DIV: case OP_REM:
        return ty->kind == TY_INT || ty->kind == TY_FLOAT;
    case OP_AND: case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR: return ty->kind == TY_INT;
    case OP_EQ: case OP_NE: return ty->kind <= TY_STR || ty->kind == TY_PTR;
    case OP_LT: case OP_LE: case OP_GT: case OP_GE:
        return ty->kind == TY_INT || ty->kind == TY_FLOAT || ty->kind == TY_STR;
    case OP_LOGICAL_AND: case OP_LOGICAL_OR: return ty->kind == TY_BOOL;
    default: return false;
    }
}

// Both operands have the same type. A literal on the left takes the type of
// the right operand: `1 + x` with `x: u8` is a `u8` addition.
static TypeId binary(Checker *c, Expr *e, TypeId expected) {
    Op op = e->as.binary.op;
    if (op == OP_COALESCE) {
        return check_coalesce(c, e, expected);
    }
    bool compares = op >= OP_EQ && op <= OP_GE;
    TypeId hint = compares || op == OP_LOGICAL_AND || op == OP_LOGICAL_OR ? NO_TYPE : expected;
    Expr *first = e->as.binary.lhs, *second = e->as.binary.rhs;
    if (is_literal(first) && !is_literal(second)) {
        first = e->as.binary.rhs;
        second = e->as.binary.lhs;
    }
    TypeId type = check_expr(c, first, hint);
    coerce(c, second, type);
    DefId id;
    const EnumInfo *info = (op == OP_EQ || op == OP_NE) ? enum_of(c, type, &id) : NULL;
    bool fieldless = info && !info->has_payload;   // enums without payloads compare tags
    uint32_t wanted = op == OP_EQ || op == OP_NE ? BOUND_EQ : compares ? BOUND_ORD : 0;
    bool bounded = wanted && (param_bounds(c, type) & wanted);   // spec 8.1
    if (!fieldless && !bounded && !operand_ok(type_get(c->types, type), op) &&
            type.id != TID_ERROR) {
        TyKind kind = type_get(c->types, type)->kind;
        if (compares && (kind == TY_ADT || kind == TY_TUPLE)) {   // derived `Eq`, `Ord` (9.9)
            check_error(c, "E0901", e->span, "comparing `%s` is not supported by stage 0",
                        type_name(c, type));
            return type_id(TID_ERROR);
        }
        return operator_error(c, e->span, type);
    }
    return compares ? type_id(TID_BOOL) : type;
}

static TypeId cast(Checker *c, Expr *e) {
    TypeId from = check_expr(c, e->as.cast.value, NO_TYPE);
    TypeId to = resolve_type(c, e->as.cast.type);
    const Expr *value = e->as.cast.value;
    bool zero = value->kind == EXPR_INT && value->as.int_value == 0;   // `0 as *T`: null
    if (pointer_cast(c, from, to, e->span, zero)) {
        return to;
    }
    bool ok = (type_is_numeric(c->types, from) && type_is_numeric(c->types, to)) ||
              type_eq(from, to) || from.id == TID_ERROR || to.id == TID_ERROR;
    if (!ok) {
        char from_name[64];
        snprintf(from_name, sizeof from_name, "%s", type_name(c, from));
        check_error(c, "E0313", e->span, "cannot cast `%s` to `%s`", from_name,
                    type_name(c, to));
    }
    return to;
}

// ---------------------------------------------------------------------------
// Calls

TypeId check_args(Checker *c, Expr *call, const Ty *signature) {
    Ty sig = *signature;   // a copy: interning may move the type table
    ArgList args = call->as.call.args;
    if (args.len != sig.arg_count) {
        check_error(c, "E0302", call->span,
                    "wrong number of arguments: expected %u, found %u", sig.arg_count, args.len);
    }
    for (uint32_t i = 0; i < args.len; i++) {
        if (args.items[i].name.id != 0) {
            unsupported(c, args.items[i].value->span, "named arguments are");
        }
        coerce(c, args.items[i].value, i < sig.arg_count ? sig.args[i] : NO_TYPE);
    }
    return sig.inner;
}

// print(str), println(str) and panic(str) !; `assert` is in std.testing.
static TypeId builtin_call(Checker *c, Expr *call, Builtin builtin) {
    TypeId param = type_id(TID_STR);
    TypeId ret = type_id(builtin == BUILTIN_PANIC ? TID_NEVER : TID_UNIT);
    Ty sig = {.kind = TY_FN, .inner = ret, .arg_count = 1, .args = &param};
    return check_args(c, call, &sig);
}

static TypeId check_fn_call(Checker *c, Expr *e, const Def *def, TypeList explicit,
                            TypeId expected) {
    if (is_unsafe_fn(def)) {
        require_unsafe(c, e->span, "calling an `unsafe` or `extern` function");
    }
    const Ty *sig = type_get(c->types, def->type);
    TypeId ret;
    if (fn_type_param_count(def) == 0) {
        if (explicit.len > 0) {
            check_error(c, "E0302", e->as.call.callee->span,
                        "`" SV_FMT "` takes no type arguments", SV_ARG(def_name(c, def)));
        }
        ret = check_args(c, e, sig);
    } else {
        Infer inf = infer_new(c, fn_generics(def));
        if (explicit.len > 0) {
            infer_explicit(c, &inf, false, explicit, e->as.call.callee->span);
        }
        ret = check_generic_args(c, e, sig, &inf, expected);
    }
    check_mut_args(c, def->fn, e);
    return ret;
}

// The definition a callee names, `f` or `f<int>`; RES_NONE otherwise.
static Res callee_res(const Expr *callee) {
    if (callee->kind == EXPR_IDENT) {
        return callee->as.ident.res;
    }
    return callee->kind == EXPR_GENERIC ? callee->as.generic.res : (Res){0};
}

static TypeId not_a_function(Checker *c, Expr *e, TypeId type) {
    if (type.id != TID_ERROR) {
        check_error(c, "E0311", e->as.call.callee->span,
                    "expression of type `%s` is not a function", type_name(c, type));
    }
    for (uint32_t i = 0; i < e->as.call.args.len; i++) {
        check_expr(c, e->as.call.args.items[i].value, NO_TYPE);
    }
    return type_id(TID_ERROR);
}

static TypeId call(Checker *c, Expr *e, TypeId expected) {
    Expr *callee = e->as.call.callee;
    if (callee->kind == EXPR_FIELD) {
        return check_method_call(c, e, expected);
    }
    Res res = callee_res(callee);
    if (res.kind != RES_DEF) {
        TypeId type = check_expr(c, callee, NO_TYPE);
        const Ty *sig = type_get(c->types, type);
        return sig->kind == TY_FN ? check_args(c, e, sig) : not_a_function(c, e, type);
    }
    TypeList explicit = callee->kind == EXPR_GENERIC ? callee->as.generic.args : (TypeList){0};
    Def *def = sema_def(c->sema, (DefId){res.index});
    if (def->kind == DEF_BUILTIN && explicit.len == 0) {
        return builtin_call(c, e, def->builtin);
    }
    if (def->kind == DEF_STRUCT) {
        callee->type = type_id(TID_UNIT);
        return check_constructor(c, e, (DefId){res.index}, explicit, expected);
    }
    if (def->kind == DEF_VARIANT && explicit.len == 0) {   // `Some(x)`
        callee->type = type_id(TID_UNIT);
        return check_prelude_variant(c, e, def, &e->as.call.args, expected);
    }
    if (def->kind == DEF_FN) {
        callee->type = fn_signature(c, def->fn);
        return check_fn_call(c, e, def, explicit, expected);
    }
    callee->type = def_value(c, def, callee->span);
    return not_a_function(c, e, callee->type);
}

// `Name<T>` outside of a call, a struct literal or `Name<T>.f()`.
static TypeId generic_value(Checker *c, Expr *e) {
    check_error(c, "E0310", e->span, "expected a value, found `" SV_FMT "` with type arguments",
                SV_ARG(symbol_name(&c->sema->session->names, e->as.generic.name)));
    return type_id(TID_ERROR);
}

// ---------------------------------------------------------------------------

// `(a, b)`: each element takes the matching element of the expected type.
static TypeId tuple(Checker *c, Expr *e, TypeId expected) {
    uint32_t count = e->as.elems.len;
    const Ty *hint = type_get(c->types, expected);
    TypeId *types = ARENA_ARRAY(&c->sema->session->arena, TypeId, count + 1);
    for (uint32_t i = 0; i < count; i++) {
        bool hinted = hint->kind == TY_TUPLE && hint->arg_count == count;
        types[i] = check_expr(c, e->as.elems.items[i], hinted ? hint->args[i] : NO_TYPE);
    }
    return type_tuple(c->types, types, count);
}

static TypeId synthesize(Checker *c, Expr *e, TypeId expected) {
    switch (e->kind) {
    case EXPR_INT: return int_literal(c, e, expected, false);
    case EXPR_FLOAT: return float_literal(c, expected);
    case EXPR_BYTE: return type_id(TID_U8);
    case EXPR_BOOL: return type_id(TID_BOOL);
    case EXPR_STR: return type_id(TID_STR);
    case EXPR_INTERP: return interpolation(c, e);
    case EXPR_IDENT: case EXPR_SELF: return name_value(c, e, expected);
    case EXPR_UNARY: return unary(c, e, expected);
    case EXPR_BINARY: return binary(c, e, expected);
    case EXPR_CAST: return cast(c, e);
    case EXPR_CALL: return call(c, e, expected);
    case EXPR_IF: return check_if(c, e, expected, true);
    case EXPR_BLOCK: return check_block(c, e->as.block, expected, true);
    case EXPR_UNSAFE: return check_unsafe_block(c, e, expected, true);
    case EXPR_TUPLE: return tuple(c, e, expected);
    case EXPR_SELF_TYPE: return type_id(TID_ERROR);   // replaced by the resolver
    case EXPR_GENERIC: return generic_value(c, e);
    case EXPR_RANGE: return unsupported(c, e->span, "ranges outside of `for` are");
    case EXPR_INDEX: return check_index(c, e);
    case EXPR_FIELD: return check_field(c, e, expected);
    case EXPR_TUPLE_FIELD: return check_tuple_field(c, e);
    case EXPR_TRY: return check_try(c, e);
    case EXPR_STRUCT: return check_struct_literal(c, e, expected);
    case EXPR_ARRAY: return check_list_literal(c, e, expected);
    case EXPR_ARRAY_REPEAT: return check_list_repeat(c, e, expected);
    case EXPR_VARIANT: return check_variant_shorthand(c, e, expected);
    case EXPR_MATCH: return check_match(c, e, expected, true);
    }
    return type_id(TID_ERROR);
}

TypeId check_unsafe_block(Checker *c, Expr *e, TypeId expected, bool as_value) {
    c->unsafe_depth++;
    TypeId type = check_block(c, e->as.block, expected, as_value);
    c->unsafe_depth--;
    return type;
}

TypeId check_expr(Checker *c, Expr *e, TypeId expected) {
    e->type = synthesize(c, e, expected);
    note_value(c, e);
    return e->type;
}

// `*mut T` converts to `*T`: giving up the right to write is always safe.
static bool weakens_pointer(Checker *c, TypeId found, TypeId expected) {
    const Ty *f = type_get(c->types, found), *x = type_get(c->types, expected);
    return f->kind == TY_PTR && x->kind == TY_PTR && f->data && !x->data &&
           type_eq(f->inner, x->inner);
}

void require_type(Checker *c, const Expr *e, TypeId found, TypeId expected) {
    if (!types_compatible(found, expected) && !weakens_pointer(c, found, expected)) {
        char expected_name[64];
        snprintf(expected_name, sizeof expected_name, "%s", type_name(c, expected));
        check_error(c, "E0300", e->span, "mismatched types: expected %s, found %s",
                    expected_name, type_name(c, found));
    }
}

TypeId coerce(Checker *c, Expr *e, TypeId expected) {
    TypeId found = check_expr(c, e, expected);
    if (!types_compatible(found, expected)) {   // the common case stays inline
        if (coerce_to_slice(c, e, found, expected)) {
            return expected;
        }
        require_type(c, e, found, expected);
    }
    return found;
}

TypeId check_const(Checker *c, Def *def) {
    if (def->typed) {
        return def->type;
    }
    if (def->checking) {
        check_error(c, "E0800", def->span, "the constant `" SV_FMT "` depends on itself",
                    SV_ARG(def_name(c, def)));
        return type_id(TID_ERROR);
    }
    def->checking = true;
    uint32_t saved_module = sema_enter(c->sema, def->module);
    Generics outer = enter_generics(c, (Generics){0});
    TypeExpr *annotation = def->item->as.const_.type;
    Expr *value = def->item->as.const_.value;
    TypeId type = annotation ? resolve_type(c, annotation) : NO_TYPE;
    TypeId found = annotation ? coerce(c, value, type) : check_expr(c, value, NO_TYPE);
    def->type = annotation ? type : found;
    if (def->kind == DEF_CONST) {
        def->value = const_eval(c, value);   // while `checking` is set: catches cycles
    } else if (!type_eq(def->type, type_id(TID_ERROR)) && !type_is_numeric(c->types, def->type) &&
               !type_eq(def->type, type_id(TID_BOOL))) {
        TypeId ty = def->type;
        char what[160];
        snprintf(what, sizeof what, "mutable globals of type `%s` are", type_name(c, ty));
        unsupported(c, def->item->span, what);
    } else if (!type_eq(def->type, type_id(TID_ERROR))) {   // the initial value, in the binary
        def->value = const_eval(c, value);
    }
    enter_generics(c, outer);
    sema_enter(c->sema, saved_module);
    def->checking = false;
    def->typed = true;
    return def->type;
}
