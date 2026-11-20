// Type checking of statements and blocks (spec 3.1 and 4).

#include "sema/check_internal.h"

static TypeId check_stmt(Checker *c, Stmt *s);

static inline void set_local(Checker *c, LocalId id, TypeId type) {
    c->local_types[id.id] = type;
    note_owned(c, type);
}

static StrView local_name(Checker *c, LocalId id) {
    return symbol_name(&c->sema->session->names, c->fn->locals.items[id.id].name);
}

static TypeId let_stmt(Checker *c, Stmt *s) {
    Pattern *pattern = s->as.let.pattern;
    TypeId type = s->as.let.type ? resolve_type(c, s->as.let.type) : NO_TYPE;
    TypeId found = s->as.let.type ? coerce(c, s->as.let.value, type)
                                  : check_expr(c, s->as.let.value, NO_TYPE);
    check_pattern(c, pattern, s->as.let.type ? type : found);
    if (s->as.let.else_) {   // `const pattern = value else { ... }` (spec 4.6)
        TypeId other = check_block(c, s->as.let.else_, NO_TYPE, false);
        if (other.id != TID_NEVER) {
            check_error(c, "E0307", s->as.let.else_->span,
                        "the `else` block of `const ... else` must not fall through: end it "
                        "with `return`, `break`, `continue` or a panic");
        }
    } else if (!pattern_irrefutable(c, pattern)) {
        check_error(c, "E0501", pattern->span,
                    "this pattern does not match every value: use `match` or `if const`");
    }
    return found.id == TID_NEVER ? found : type_id(TID_UNIT);
}

static TypeId var_stmt(Checker *c, Stmt *s) {
    TypeExpr *annotation = s->as.var.type;
    Expr *value = s->as.var.value;
    TypeId type = annotation ? resolve_type(c, annotation) : NO_TYPE;
    TypeId found = NO_TYPE;
    if (value) {
        found = annotation ? coerce(c, value, type) : check_expr(c, value, NO_TYPE);
    }
    set_local(c, s->as.var.local, annotation ? type : found);
    c->owned_values = c->owned_values || !s->as.var.value;   // may be uninitialized
    return found.id == TID_NEVER ? found : type_id(TID_UNIT);
}

// The left side of `=` must be a mutable place (spec 4.2).
static void check_place(Checker *c, Expr *target) {
    Expr *root = place_root(target);
    if (root && (root != target || root->kind == EXPR_UNARY)) {   // a field or `*p`
        require_mutable(c, target);
        return;
    }
    if (!root) {
        unsupported(c, target->span, "assigning to elements is");
        return;
    }
    Res res = target->as.ident.res;
    if (res.kind == RES_LOCAL && !c->fn->locals.items[res.index].mutable) {
        StrView local = local_name(c, (LocalId){res.index});
        check_error(c, "E0304", target->span,
                    "cannot assign twice to immutable variable `" SV_FMT "`", SV_ARG(local));
        diag_help(&c->sema->session->diags, "declare it with `let " SV_FMT "` to assign to it "
                  "again", SV_ARG(local));
    } else if (res.kind == RES_DEF && sema_def(c->sema, (DefId){res.index})->kind != DEF_GLOBAL) {
        check_error(c, "E0304", target->span, "cannot assign to `" SV_FMT "`",
                    SV_ARG(symbol_name(&c->sema->session->names, target->as.ident.name)));
    }
}

static TypeId assign_stmt(Checker *c, Stmt *s) {
    Expr *target = s->as.assign.target;
    TypeId type = check_expr(c, target, NO_TYPE);
    check_place(c, target);
    if (s->as.assign.op == OP_ASSIGN) {
        coerce(c, s->as.assign.value, type);
        return type_id(TID_UNIT);
    }
    // `x op= y` checks like `x op y`.
    Expr op = {.kind = EXPR_BINARY, .span = s->span};
    op.as.binary.op = s->as.assign.op;
    op.as.binary.lhs = target;
    op.as.binary.rhs = s->as.assign.value;
    coerce(c, &op, type);
    return type_id(TID_UNIT);
}

static TypeId loop_body(Checker *c, Block *body, bool infinite) {
    VEC_PUSH(c->loops, false);
    check_block(c, body, NO_TYPE, false);
    bool broken = VEC_POP(c->loops);
    // An infinite loop without `break` never finishes.
    return infinite && !broken ? type_id(TID_NEVER) : type_id(TID_UNIT);
}

static TypeId for_in(Checker *c, Stmt *s) {
    Expr *range = s->as.for_.expr;
    if (range->kind != EXPR_RANGE) {   // `for x in list`, `for key, value in map`
        check_for_collection(c, s);
        return loop_body(c, s->as.for_.body, false);
    }
    if (s->as.for_.second.id != 0) {
        check_error(c, "E0300", range->span, "a range gives one variable: `for i in a..b`");
        return type_id(TID_UNIT);
    }
    if (!range->as.range.lo || !range->as.range.hi) {
        unsupported(c, range->span, "`for` over ranges without both bounds is");
        return type_id(TID_UNIT);
    }
    // A literal bound takes the type of the other one: `for i in 0..n` with `n: u8`.
    Expr *first = range->as.range.lo, *second = range->as.range.hi;
    if (first->kind == EXPR_INT && second->kind != EXPR_INT) {
        first = range->as.range.hi;
        second = range->as.range.lo;
    }
    TypeId type = check_expr(c, first, NO_TYPE);
    coerce(c, second, type);
    if (!type_is_integer(c->types, type) && type.id != TID_ERROR) {
        check_error(c, "E0300", range->span, "mismatched types: expected an integer, found %s",
                    type_name(c, type));
    }
    range->type = type;
    set_local(c, s->as.for_.first_local, type);
    return loop_body(c, s->as.for_.body, false);
}

static TypeId for_stmt(Checker *c, Stmt *s) {
    switch (s->as.for_.kind) {
    case FOR_INFINITE: return loop_body(c, s->as.for_.body, true);
    case FOR_COND:
        coerce(c, s->as.for_.expr, type_id(TID_BOOL));
        return loop_body(c, s->as.for_.body, false);
    case FOR_IN: return for_in(c, s);
    }
    return type_id(TID_UNIT);   // GCOVR_EXCL_LINE: every for kind returns from its case
}

static TypeId return_stmt(Checker *c, Stmt *s) {
    if (s->as.expr) {
        coerce(c, s->as.expr, c->ret);
        check_returned_ref(c, s->as.expr);
    } else if (!types_compatible(type_id(TID_UNIT), c->ret)) {
        check_error(c, "E0300", s->span, "mismatched types: expected %s, found ()",
                    type_name(c, c->ret));
    }
    return type_id(TID_NEVER);
}

static TypeId jump_stmt(Checker *c, Stmt *s) {
    if (c->loops.len == 0) {
        check_error(c, "E0309", s->span, "`%s` outside of a loop",
                    s->kind == STMT_BREAK ? "break" : "continue");
    } else if (s->kind == STMT_BREAK) {
        VEC_LAST(c->loops) = true;
    }
    return type_id(TID_NEVER);
}

// An `if`, `match` or block used as a statement: its branches are not values.
static TypeId expr_stmt(Checker *c, Expr *e) {
    if (e->kind == EXPR_MATCH) {
        e->type = check_match(c, e, NO_TYPE, false);
    } else if (e->kind == EXPR_IF) {
        e->type = check_if(c, e, NO_TYPE, false);
    } else if (e->kind == EXPR_UNSAFE) {
        e->type = check_unsafe_block(c, e, NO_TYPE, false);
    } else if (e->kind == EXPR_BLOCK) {
        e->type = check_block(c, e->as.block, NO_TYPE, false);
    } else {
        check_expr(c, e, NO_TYPE);
        check_unused_result(c, e);
    }
    return e->type.id == TID_NEVER ? e->type : type_id(TID_UNIT);
}

static TypeId check_stmt(Checker *c, Stmt *s) {
    switch (s->kind) {
    case STMT_LET: return let_stmt(c, s);
    case STMT_VAR: return var_stmt(c, s);
    case STMT_ASSIGN: return assign_stmt(c, s);
    case STMT_FOR: return for_stmt(c, s);
    case STMT_RETURN: return return_stmt(c, s);
    case STMT_BREAK: case STMT_CONTINUE: return jump_stmt(c, s);
    case STMT_EXPR: return expr_stmt(c, s->as.expr);
    }
    return type_id(TID_UNIT);   // GCOVR_EXCL_LINE: every statement kind returns from its case
}

// As a value, a block is worth its last expression (spec 4.1). A block that
// cannot finish has type `!`.
TypeId check_block(Checker *c, Block *block, TypeId expected, bool as_value) {
    StmtList stmts = block->stmts;
    bool diverges = false, warned = false;
    for (uint32_t i = 0; i < stmts.len; i++) {
        Stmt *s = stmts.items[i];
        if (diverges && !warned) {   // W0004 once per block (spec 12.6)
            diag_warning(&c->sema->session->diags, "W0004", s->span,
                         "unreachable code: the statement before it never finishes");
            warned = true;
        }
        bool tail = as_value && i + 1 == stmts.len && s->kind == STMT_EXPR;
        TypeId type = tail ? check_expr(c, s->as.expr, expected) : check_stmt(c, s);
        if (tail) {
            return diverges ? type_id(TID_NEVER) : type;
        }
        diverges = diverges || type.id == TID_NEVER;
    }
    return diverges ? type_id(TID_NEVER) : type_id(TID_UNIT);
}

static TypeId join(Checker *c, Expr *e, TypeId expected, TypeId then, TypeId else_) {
    if (then.id == TID_NEVER) {
        return else_;
    }
    if (else_.id != TID_NEVER && !types_compatible(else_, then)) {
        char then_name[64];
        snprintf(then_name, sizeof then_name, "%s", type_name(c, then));
        mismatched_branches(c, e->as.if_.else_->span, expected, then_name, type_name(c, else_));
    }
    return then;
}

TypeId check_if(Checker *c, Expr *e, TypeId expected, bool as_value) {
    if (e->as.if_.let_pattern) {   // `if const pattern = value`
        check_pattern(c, e->as.if_.let_pattern, check_expr(c, e->as.if_.cond, NO_TYPE));
    } else {
        coerce(c, e->as.if_.cond, type_id(TID_BOOL));
    }
    TypeId then = check_block(c, e->as.if_.then, expected, as_value);
    Expr *else_ = e->as.if_.else_;
    if (!else_) {
        return type_id(TID_UNIT);   // as a value, `if` without `else` is ()
    }
    TypeId other = else_->kind == EXPR_IF ? check_if(c, else_, expected, as_value)
                                          : check_block(c, else_->as.block, expected, as_value);
    else_->type = other;
    if (!as_value) {
        return then.id == TID_NEVER && other.id == TID_NEVER ? type_id(TID_NEVER)
                                                             : type_id(TID_UNIT);
    }
    return join(c, e, expected, then, other);
}
