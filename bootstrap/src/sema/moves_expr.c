// Move analysis of expressions and places (spec 5.1). Each expression is
// walked as a *read*, which only uses its value (an argument of a borrowed
// parameter, an operand, a condition), or as a *move*, which consumes it
// (a `const`, a `move` parameter, a field of a literal, a return value).
// Moving a place of a non-Copy type marks it moved; any later use of it,
// or of a part of it, is E0600 until it is assigned again.

#include "sema/moves_internal.h"

static StrView name(Moves *m, uint32_t local) {
    return symbol_name(&m->c->sema->session->names, m->fn->locals.items[local].name);
}

bool is_copy(Moves *m, TypeId type) {
    return type_is_copy(m->c, type);
}

// The place of `e`, walking the bases it does not reach through fields.
Place place_of(Moves *m, Expr *e, State *s) {
    if (e->kind == EXPR_IDENT || e->kind == EXPR_SELF) {   // else a constant or a function
        bool local = e->as.ident.res.kind == RES_LOCAL;
        return (Place){.local = local, .part = {.local = local ? e->as.ident.res.index : 0}};
    }
    bool field = e->kind == EXPR_FIELD && e->as.field.res.kind == RES_FIELD;
    if (field || e->kind == EXPR_TUPLE_FIELD) {
        Place base = place_of(m, field ? e->as.field.base : e->as.tuple_field.base, s);
        if (base.local && base.part.len < PART_DEPTH) {
            base.part.fields[base.part.len++] = field ? e->as.field.res.index
                                                      : e->as.tuple_field.index;
        }
        return base;
    }
    if (e->kind == EXPR_INDEX) {
        Place base = place_of(m, e->as.index.base, s);
        moves_expr(m, e->as.index.index, s, USE_READ);
        base.via = VIA_INDEX;
        return base;
    }
    if (e->kind == EXPR_UNARY && e->as.unary.op == OP_DEREF) {
        moves_expr(m, e->as.unary.operand, s, USE_READ);
        const Ty *ty = type_get(m->c->types, e->as.unary.operand->type);
        // The value in a box (`*b.ptr` once checked) is lent like a referenced
        // one: a `match` on it binds by borrow, as the box still owns it.
        // In std.core.box itself, `*self.ptr` is the raw pointer it is.
        Sema *sema = m->c->sema;
        bool box_module = sema->box.id && sema_def(sema, sema->box)->module == sema->current;
        bool raw = ty->kind == TY_PTR && (box_module || !is_box_deref(m->c, e));
        return (Place){.via = raw ? VIA_RAW : VIA_DEREF};
    }
    moves_expr(m, e, s, USE_READ);   // a temporary: `make().x`
    return (Place){0};
}

void use_place(Moves *m, const Place *p, State *s, Span span) {
    if (!p->local) {
        return;
    }
    if (bit(s->uninit, p->part.local)) {
        move_error(m, "E0601", span, "use of possibly uninitialized variable `" SV_FMT "`",
                   SV_ARG(name(m, p->part.local)));
    } else if (moved_part(s, &p->part)) {
        StrView local = name(m, p->part.local);
        move_error(m, "E0600", span, "use of moved value `" SV_FMT "`", SV_ARG(local));
        if (m->report) {
            diag_help(&m->c->sema->session->diags, "`" SV_FMT "` was moved before: clone it "
                      "there if both copies are needed", SV_ARG(local));
        }
    }
}

// Moving out of `p`: only out of a place the function owns entirely.
static void move_place(Moves *m, const Place *p, State *s, Span span) {
    if (p->via == VIA_INDEX || p->via == VIA_DEREF) {
        move_error(m, "E0602", span, "cannot move out of %s: use `remove`, `swapRemove`, "
                   "`mem.replace` or `.clone()`", p->via == VIA_INDEX ? "an element of a "
                   "collection" : "a reference");
        return;
    }
    if (!p->local || p->via == VIA_RAW) {
        return;   // a temporary, or a raw pointer read (unsafe)
    }
    if (bit(m->borrowed, p->part.local)) {
        move_error(m, "E0602", span, "cannot move out of `" SV_FMT "`, which is borrowed: "
                   "declare the parameter `move`, or call `.clone()`",
                   SV_ARG(name(m, p->part.local)));
        return;
    }
    move_part(m, s, &p->part);
}

bool is_place(const Expr *e) {
    return e->kind == EXPR_IDENT || e->kind == EXPR_SELF || e->kind == EXPR_TUPLE_FIELD ||
           e->kind == EXPR_INDEX || (e->kind == EXPR_FIELD && e->as.field.res.kind == RES_FIELD) ||
           (e->kind == EXPR_UNARY && e->as.unary.op == OP_DEREF);
}

// `p = value`: `p` holds a value again. A field of a moved struct cannot be
// assigned: the struct as a whole is gone.
static void assign_place(Moves *m, Expr *e, const Place *p, State *s, Span span) {
    if (!p->local) {
        return;
    }
    // The old value of a part that may have been moved out is not dropped.
    e->moves = e->moves || moved_part(s, &p->part);
    if (p->part.len > 0 && bit(s->moved, p->part.local)) {
        use_place(m, &(Place){.local = true, .part = {.local = p->part.local}}, s, span);
    }
    init_part(m, s, &p->part);
}

// Moving a field out of a value whose type has a `drop` method would leave
// that method a value with a hole (E0602).
static bool has_drop(Moves *m, const Expr *e) {
    bool field = e->kind == EXPR_FIELD && e->as.field.res.kind == RES_FIELD;
    if (!field && e->kind != EXPR_TUPLE_FIELD) {
        return false;
    }
    TypeId base = field ? e->as.field.base->type : e->as.tuple_field.base->type;
    const Ty *ty = type_get(m->c->types, base);
    if (ty->kind != TY_ADT) {
        return false;
    }
    Symbol drop = intern(&m->c->sema->session->names, SV("drop"));
    if (!has_method(m->c, (DefId){ty->data}, drop)) {
        return false;
    }
    move_error(m, "E0602", e->span, "cannot move out of a field of `%s`, which has a `drop` "
               "method: use `mem.replace`", type_name(m->c, base));
    return true;
}

static void consume_root(Expr *e) {
    while (e->kind == EXPR_FIELD || e->kind == EXPR_TUPLE_FIELD) {
        e = e->kind == EXPR_FIELD ? e->as.field.base : e->as.tuple_field.base;
    }
    if (!is_place(e)) {
        e->consumed = true;
    }
}

static void place_expr(Moves *m, Expr *e, State *s, Use use) {
    Place p = place_of(m, e, s);
    if (use == USE_ASSIGN) {   // the target of `=`: overwritten, not read
        assign_place(m, e, &p, s, e->span);
        return;
    }
    use_place(m, &p, s, e->span);
    if (use == USE_MOVE && !is_copy(m, e->type) && !p.local) {
        consume_root(e);   // a field moved out of a temporary: the rest is not dropped
    }
    if (use == USE_MOVE && !is_copy(m, e->type) && has_drop(m, e)) {
        return;   // E0602: a type with a `drop` method is dropped whole
    }
    if (use == USE_MOVE && !is_copy(m, e->type)) {
        move_place(m, &p, s, e->span);
        e->moves = e->moves || (p.local && p.via == VIA_NOTHING);   // for the drop flags
    }
}

// ---------------------------------------------------------------------------
// Calls

static void each_arg(Moves *m, ArgList args, const FnDecl *fn, Use use, State *s) {
    for (uint32_t i = 0; i < args.len; i++) {
        Use arg = use;
        if (fn && i < fn->params.len) {
            arg = fn->params.items[i].mode == MODE_MOVE ? USE_MOVE : USE_READ;
        }
        moves_expr(m, args.items[i].value, s, arg);
    }
}

// A call consumes the arguments of `move` parameters (and of constructors
// and variants) and only reads the others.
static void call(Moves *m, Expr *e, State *s) {
    Expr *callee = e->as.call.callee;
    ArgList args = e->as.call.args;
    if (callee->kind == EXPR_FIELD) {
        Res res = callee->as.field.res;
        if (res.kind == RES_DEF) {   // a method, or an associated function
            FnDecl *fn = sema_def(m->c->sema, (DefId){res.index})->fn;
            if (fn->has_self) {
                Use self = fn->self_mode == MODE_MOVE ? USE_MOVE : USE_READ;
                moves_expr(m, callee->as.field.base, s, self);
            }
            each_arg(m, args, fn, USE_READ, s);
            return;
        }
        if (res.kind != RES_VARIANT) {   // the built-in methods read
            moves_expr(m, callee->as.field.base, s, USE_READ);
        }
        each_arg(m, args, NULL, res.kind == RES_VARIANT ? USE_MOVE : USE_READ, s);
        return;
    }
    Res res = callee->kind == EXPR_GENERIC ? callee->as.generic.res : callee->as.ident.res;
    const Def *def = res.kind == RES_DEF ? sema_def(m->c->sema, (DefId){res.index}) : NULL;
    if (!def) {
        moves_expr(m, callee, s, USE_READ);
    }
    bool build = def && (def->kind == DEF_STRUCT || def->kind == DEF_VARIANT);
    each_arg(m, args, def && def->kind == DEF_FN ? def->fn : NULL,
             build ? USE_MOVE : USE_READ, s);
}

// ---------------------------------------------------------------------------

static void each(Moves *m, ExprList list, State *s, Use use) {
    for (uint32_t i = 0; i < list.len; i++) {
        moves_expr(m, list.items[i], s, use);
    }
}

// `a && b`, `a || b`, `a ?? b`: `b` is evaluated on some paths only.
static void conditional(Moves *m, Expr *e, State *s, Use lhs, Use rhs) {
    moves_expr(m, e->as.binary.lhs, s, lhs);
    State other = state_copy(m, s);
    moves_expr(m, e->as.binary.rhs, &other, rhs);
    state_join(m, s, &other);
}

static void binary(Moves *m, Expr *e, State *s) {
    Op op = e->as.binary.op;
    if (op == OP_LOGICAL_AND || op == OP_LOGICAL_OR) {
        conditional(m, e, s, USE_READ, USE_READ);
    } else if (op == OP_COALESCE) {
        conditional(m, e, s, USE_MOVE, USE_MOVE);
    } else {
        moves_expr(m, e->as.binary.lhs, s, USE_READ);
        moves_expr(m, e->as.binary.rhs, s, USE_READ);
    }
}

static void unary(Moves *m, Expr *e, State *s) {
    if (e->as.unary.op == OP_REF || e->as.unary.op == OP_REF_MUT) {   // a borrow
        Expr *operand = e->as.unary.operand;
        if (is_place(operand)) {
            Place p = place_of(m, operand, s);
            use_place(m, &p, s, operand->span);
        } else {
            moves_expr(m, operand, s, USE_READ);
        }
        return;
    }
    moves_expr(m, e->as.unary.operand, s, USE_READ);
}

static void literal(Moves *m, Expr *e, State *s) {
    for (uint32_t i = 0; i < e->as.struct_.fields.len; i++) {
        moves_expr(m, e->as.struct_.fields.items[i].value, s, USE_MOVE);
    }
    if (e->as.struct_.base) {
        moves_expr(m, e->as.struct_.base, s, USE_MOVE);
    }
}

static void walk(Moves *m, Expr *e, State *s) {
    switch (e->kind) {
    case EXPR_INTERP: each(m, e->as.parts, s, USE_READ); break;
    case EXPR_UNARY: unary(m, e, s); break;
    case EXPR_BINARY: binary(m, e, s); break;
    case EXPR_CAST: moves_expr(m, e->as.cast.value, s, USE_READ); break;
    case EXPR_RANGE:
        if (e->as.range.lo) {
            moves_expr(m, e->as.range.lo, s, USE_READ);
        }
        if (e->as.range.hi) {
            moves_expr(m, e->as.range.hi, s, USE_READ);
        }
        break;
    case EXPR_CALL: call(m, e, s); break;
    case EXPR_TRY: moves_expr(m, e->as.inner, s, USE_MOVE); break;
    case EXPR_STRUCT: literal(m, e, s); break;
    case EXPR_ARRAY: case EXPR_TUPLE: each(m, e->as.elems, s, USE_MOVE); break;
    case EXPR_ARRAY_REPEAT:
        moves_expr(m, e->as.repeat.value, s, USE_READ);
        moves_expr(m, e->as.repeat.count, s, USE_READ);
        break;
    // GCOVR_EXCL_START: E0326 rejects an unqualified variant
    case EXPR_VARIANT:
        each_arg(m, e->as.variant.args, NULL, USE_MOVE, s);
        break;
    // GCOVR_EXCL_STOP
    // The value of an `if`, a `match` or a block is a new value, which its
    // user consumes or drops: the last expression of each branch is moved
    // into it, whatever the use (spec 5.1).
    case EXPR_IF: moves_if(m, e, s, USE_MOVE); break;
    case EXPR_MATCH: moves_match(m, e, s, USE_MOVE); break;
    case EXPR_BLOCK: case EXPR_UNSAFE: moves_block(m, e->as.block, s, USE_MOVE); break;
    default: break;   // literals, names of types and functions, constants
    }
}

void moves_expr(Moves *m, Expr *e, State *s, Use use) {
    if (s->dead) {
        return;   // unreachable code moves nothing
    }
    if (is_place(e)) {
        place_expr(m, e, s, use);
    } else {
        e->consumed = e->consumed || use == USE_MOVE;   // else a temporary, dropped (3.2)
        walk(m, e, s);
    }
    if (e->type.id == TID_NEVER) {
        s->dead = true;
    }
}
