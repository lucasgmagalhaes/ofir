// Lowering of expressions to operands.

#include "air/lower_internal.h"
#include "sema/const_eval.h"

bool type_is_value(TypeId type) {
    return type.id != TID_UNIT && type.id != TID_NEVER && type.id != TID_ERROR;
}

static Operand int_literal(Lowerer *l, Expr *e) {
    if (type_get(l->types, e->type)->kind == TY_FLOAT) {   // `let x: float = 0`
        return (Operand){.kind = OPERAND_FLOAT, .type = e->type,
                         .as.float_value = (double)e->as.int_value};
    }
    return op_int(e->as.int_value, e->type);
}

static Operand const_operand(const ConstValue *v, TypeId type) {
    switch (v->kind) {
    case CV_INT: return op_int(v->bits, type);
    case CV_FLOAT: return (Operand){.kind = OPERAND_FLOAT, .type = type, .as.float_value = v->f};
    case CV_BOOL: return op_bool(v->b);
    case CV_STR: return (Operand){.kind = OPERAND_STR, .type = type, .as.str = v->s};
    }
    return op_unit();
}

static Operand def_value(Lowerer *l, Expr *e, Def *def) {
    if (def->kind == DEF_CONST) {   // evaluated by the checker (sema/const_eval.c)
        return const_operand(def->value, e->type);
    }
    if (def->kind == DEF_VARIANT) {   // `None`
        return build_variant(l, e->type, def->variant, (ArgList){0});
    }
    diag_error(&l->sema->session->diags, "E0901", e->span,
               "global variables are not supported yet");
    return op_unit();
}

static Operand name(Lowerer *l, Expr *e) {
    Res res = e->as.ident.res;
    if (res.kind == RES_LOCAL) {
        return place_operand(lower_place(l, e), OPERAND_COPY, e->type);
    }
    return def_value(l, e, sema_def(l->sema, (DefId){res.index}));
}

static Operand call(Lowerer *l, Expr *e) {
    Expr *callee = e->as.call.callee;
    if (callee->kind == EXPR_FIELD) {
        return lower_method_call(l, e);
    }
    Res res = callee->kind == EXPR_GENERIC ? callee->as.generic.res : callee->as.ident.res;
    Def *def = sema_def(l->sema, (DefId){res.index});
    ArgList args = e->as.call.args;
    if (def->kind == DEF_STRUCT) {
        return lower_constructor(l, e);
    }
    if (def->kind == DEF_VARIANT) {   // `Some(x)`
        return build_variant(l, e->type, def->variant, args);
    }
    if (def->kind == DEF_BUILTIN) {
        if (def->builtin == BUILTIN_PANIC) {
            lower_panic_assert(l, e, def->builtin);
        } else {
            lower_print(l, args.items[0].value, def->builtin == BUILTIN_PRINTLN);
        }
        return op_unit();
    }
    return emit_user_call(l, e, def->fn, NULL);
}

// An argument: the address of the place for a `mut` parameter, else the
// value.
static Operand argument(Lowerer *l, Expr *arg, bool by_ref, const Expr *next) {
    if (by_ref) {
        return place_operand(lower_place(l, arg), OPERAND_ADDR, arg->type);
    }
    Operand value = lower_expr(l, arg);
    return next ? settle(l, value, next) : value;
}

void emit_direct_call(Lowerer *l, const FnDecl *fn, TypeId targs, CallArgs args,
                      LocalId *dest) {
    AirStmt s = {.kind = AIR_CALL, .has_dest = dest != NULL};
    if (dest) {
        s.dest = *dest;
    }
    s.as.call.fn = fn->def;
    s.as.call.targs = targs;
    s.as.call.args = args.items;
    s.as.call.arg_count = args.count;
    air_push(l->body, l->block, s);
}

FnDecl *std_fn(Lowerer *l, const char *module, const char *name) {
    for (uint32_t m = 0; m < l->sema->program.len; m++) {
        if (sv_eq(l->sema->program.items[m].name, sv_from_cstr(module))) {
            DefId id = scope_module_in(&l->sema->scope, m,
                                       intern(&l->sema->session->names, sv_from_cstr(name)));
            if (id.id && sema_def(l->sema, id)->fn) {
                return sema_def(l->sema, id)->fn;
            }
        }
    }
    fatal("lowering: the standard library has no `%s.%s`", module, name);
}

bool by_address(const FnDecl *fn, int32_t index) {
    Mode mode = index < 0 ? fn->self_mode : fn->params.items[index].mode;
    return mode == MODE_MUT ||
           (fn->returns_borrow && fn->source_by_ref && fn->borrow_source == index);
}

// A call of a user function or method; the receiver of a method is the
// first argument.
Operand emit_user_call(Lowerer *l, Expr *e, FnDecl *fn, Expr *receiver) {
    const Def *def = sema_def(l->sema, fn->def);
    if (def->item && def->item->kind == ITEM_EXTERN &&
        sv_eq(def->item->as.extern_.abi, SV("intrinsic"))) {
        return lower_intrinsic(l, e, fn);
    }
    ArgList args = e->as.call.args;
    uint32_t first = receiver ? 1 : 0;
    AirStmt s = {.kind = AIR_CALL};
    s.as.call.fn = fn->def;   // reached by mono_body, once the type arguments are known
    s.as.call.targs = fn_type_param_count(def) > 0 ? e->as.call.type_args : type_id(TID_UNIT);
    s.as.call.arg_count = args.len + first;
    s.as.call.args = ARENA_ARRAY(&l->sema->session->arena, Operand, args.len + first + 1);
    if (receiver) {
        const Expr *next = args.len ? args.items[0].value : NULL;
        s.as.call.args[0] = argument(l, receiver, by_address(fn, -1), next);
    }
    for (uint32_t i = 0; i < args.len; i++) {
        const Expr *next = i + 1 < args.len ? args.items[i + 1].value : NULL;
        bool by_ref = i < fn->params.len && by_address(fn, (int32_t)i);
        s.as.call.args[first + i] = argument(l, args.items[i].value, by_ref, next);
    }
    Operand result = op_unit();
    if (type_is_value(e->type)) {
        s.dest = new_temp(l, e->type);
        s.has_dest = true;
        result = op_copy(s.dest, e->type);
    }
    air_push(l->body, l->block, s);
    if (e->type.id == TID_NEVER) {
        terminate(l, (Terminator){.kind = TERM_UNREACHABLE});
    }
    return result;
}

// Assigns `value` to `dest` when the expression has a value.
static void store(Lowerer *l, LocalId dest, bool has_dest, Operand value) {
    if (has_dest && value.kind != OPERAND_UNIT) {
        emit_assign(l, dest, (Rvalue){.kind = RV_USE, .a = value});
    }
}

// A condition, whose temporaries end with it.
Operand lower_condition(Lowerer *l, Expr *e) {
    temp_scope_enter(l);
    Operand cond = lower_expr(l, e);
    cond = op_copy(to_local(l, cond), cond.type);
    temp_scope_leave(l);
    return cond;
}

static Operand lower_if(Lowerer *l, Expr *e) {
    bool has_value = type_is_value(e->type);
    LocalId dest = has_value ? new_temp(l, e->type) : (LocalId){0};
    Operand cond = e->as.if_.let_pattern ? lower_expr(l, e->as.if_.cond)
                                         : lower_condition(l, e->as.if_.cond);
    BlockId then = air_new_block(l->body), other = air_new_block(l->body);
    BlockId join = air_new_block(l->body);
    Place place = {0};
    bool consumed = e->as.if_.cond->consumed || e->as.if_.cond->moves;
    if (e->as.if_.let_pattern) {   // the pattern's tests jump to `else` on a mismatch
        place = (Place){.local = to_local(l, cond)};
        lower_pattern(l, e->as.if_.let_pattern, place, other);
        emit_goto(l, then);
    } else {
        terminate(l, (Terminator){.kind = TERM_BRANCH, .cond = cond, .target = then,
                                  .other = other});
    }
    switch_to(l, then);
    // A consumed value: its unbound parts end with `then`, all of it with `else`.
    bool keep = e->as.if_.let_pattern && consumed;
    temp_scope_enter(l);
    if (keep) {
        keep_unbound(l, e->as.if_.let_pattern, place);
    }
    drop_scope_enter(l);   // the bindings of `if const` live in the `then` block
    if (e->as.if_.let_pattern) {
        own_bindings(l, e->as.if_.let_pattern);
    }
    Operand then_value = lower_block(l, e->as.if_.then, has_value);
    store(l, dest, has_value, then_value);
    drop_scope_leave(l);
    temp_scope_leave(l);
    emit_goto(l, join);
    switch_to(l, other);
    temp_scope_enter(l);
    if (keep) {
        keep_place(l, place, concrete_type(l, e->as.if_.let_pattern->type));
    }
    Expr *else_ = e->as.if_.else_;
    if (else_) {
        Operand value = else_->kind == EXPR_IF ? lower_if(l, else_)
                                               : lower_block(l, else_->as.block, has_value);
        store(l, dest, has_value, value);
    }
    temp_scope_leave(l);
    emit_goto(l, join);
    switch_to(l, join);
    return has_value ? op_copy(dest, e->type) : op_unit();
}

// `a && b` and `a || b` evaluate `b` only when needed.
static Operand logical(Lowerer *l, Expr *e) {
    bool is_and = e->as.binary.op == OP_LOGICAL_AND;
    LocalId dest = new_temp(l, type_id(TID_BOOL));
    emit_assign(l, dest, (Rvalue){.kind = RV_USE, .a = lower_expr(l, e->as.binary.lhs)});
    BlockId rhs = air_new_block(l->body), join = air_new_block(l->body);
    Operand lhs = op_copy(dest, type_id(TID_BOOL));
    terminate(l, (Terminator){.kind = TERM_BRANCH, .cond = lhs, .target = is_and ? rhs : join,
                              .other = is_and ? join : rhs});
    switch_to(l, rhs);
    temp_scope_enter(l);   // evaluated on one path only
    emit_assign(l, dest, (Rvalue){.kind = RV_USE, .a = lower_expr(l, e->as.binary.rhs)});
    temp_scope_leave(l);
    emit_goto(l, join);
    switch_to(l, join);
    return lhs;
}

static Operand binary(Lowerer *l, Expr *e) {
    Op op = e->as.binary.op;
    if (op == OP_LOGICAL_AND || op == OP_LOGICAL_OR) {
        return logical(l, e);
    }
    if (op == OP_COALESCE) {
        return lower_coalesce(l, e);
    }
    Operand a = settle(l, lower_expr(l, e->as.binary.lhs), e->as.binary.rhs);
    Operand b = lower_expr(l, e->as.binary.rhs);
    TypeId type = e->as.binary.lhs->type;
    if (type_get(l->types, type)->has_param) {   // a bounded parameter (spec 8.1)
        const Ty *targs = type_get(l->types, l->body->targs);
        type = type_subst(l->types, type, targs->args, targs->arg_count);
        a.type = b.type = type;
        if ((op == OP_EQ || op == OP_NE) && type_get(l->types, type)->kind == TY_TUPLE) {
            Operand same = lower_equal_values(l, a, b, type);
            if (op == OP_EQ) {
                return same;
            }
            Rvalue not = {.kind = RV_UNARY, .op = OP_NOT, .a = same};
            return op_copy(emit_temp(l, type_id(TID_BOOL), not), type_id(TID_BOOL));
        }
    }
    return lower_binary_op(l, (Arith){.op = op, .type = type, .a = a, .b = b, .span = e->span});
}

static Operand lower_kind(Lowerer *l, Expr *e);

Operand lower_expr(Lowerer *l, Expr *e) {
    bool forwarded = l->forward;   // only for `e` itself, not its parts
    l->forward = false;
    Operand value = lower_kind(l, e);
    if (e->moves) {   // the local's value is moved out: it is not dropped
        note_moved(l, e);
    }
    l->forward = forwarded;
    return own_temp(l, e, value);   // a temporary, if no one consumes it
}

static Operand lower_kind(Lowerer *l, Expr *e) {
    switch (e->kind) {
    case EXPR_INT: return int_literal(l, e);
    case EXPR_FLOAT:
        return (Operand){.kind = OPERAND_FLOAT, .type = e->type,
                         .as.float_value = e->as.float_value};
    case EXPR_BYTE: return op_int(e->as.byte_value, e->type);
    case EXPR_BOOL: return op_bool(e->as.bool_value);
    case EXPR_STR: return (Operand){.kind = OPERAND_STR, .type = e->type, .as.str = e->as.str};
    case EXPR_INTERP: return lower_interpolation(l, e);
    case EXPR_IDENT: case EXPR_SELF: return name(l, e);
    case EXPR_UNARY: return lower_unary(l, e);
    case EXPR_BINARY: return binary(l, e);
    case EXPR_CAST: return lower_cast(l, lower_expr(l, e->as.cast.value), e->type, e->span);
    case EXPR_CALL: return call(l, e);
    case EXPR_FIELD:
        if (e->as.field.res.kind == RES_VARIANT) {   // `Shape.Empty`
            return build_variant(l, e->type, e->as.field.res.index, (ArgList){0});
        }
        return e->as.field.res.kind == RES_FIELD ? lower_field_read(l, e) : lower_field(l, e);
    case EXPR_VARIANT:
        return build_variant(l, e->type, e->as.variant.index, e->as.variant.args);
    case EXPR_TUPLE: return lower_tuple(l, e);
    case EXPR_MATCH: return lower_match(l, e);
    case EXPR_TUPLE_FIELD: return lower_field_read(l, e);
    case EXPR_STRUCT: return lower_struct_literal(l, e);
    case EXPR_INDEX: return lower_index(l, e);
    case EXPR_ARRAY: return lower_list_literal(l, e);
    case EXPR_TRY: return lower_try(l, e);
    case EXPR_ARRAY_REPEAT: return lower_list_repeat(l, e);
    case EXPR_IF: return lower_if(l, e);
    case EXPR_BLOCK: case EXPR_UNSAFE:
        return lower_block(l, e->as.block, type_is_value(e->type));
    default:
        return op_unit();   // () and the constructs the checker rejects
    }
}
