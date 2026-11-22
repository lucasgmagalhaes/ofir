// References (spec 5.6): `&place`, `&mut place`, and the rules that keep
// references second-class (ADR 0002). Reading and writing through them is
// in check_unsafe.c (`*r`) and check_box.c (`r.field`, `r.method()`).
//
// Stage 0 has no borrow checker yet (roadmap 4.x): references are not
// checked against moves and mutations of what they borrow.

#include "sema/check_internal.h"

// `&x` or `&mut x`. The operand is a place, or a temporary that lives until
// the end of the statement. Stage 0 treats `&str` as `str`: a view with
// `cap == 0`, which a modification copies first, like a literal.
TypeId check_ref(Checker *c, Expr *e, TypeId expected) {
    Expr *operand = e->as.unary.operand;
    bool mutable = e->as.unary.op == OP_REF_MUT;
    const Ty *hint = type_get(c->types, expected);
    TypeId type = check_expr(c, operand, hint->kind == TY_REF ? hint->inner : NO_TYPE);
    if (mutable) {
        require_mutable(c, operand);
    }
    if (type.id == TID_STR) {
        if (mutable) {
            return unsupported(c, e->span, "`&mut str` is");
        }
        return type;
    }
    return type.id == TID_ERROR ? type : type_ref(c->types, type, mutable);
}

// Whether a value of `type` holds a reference: `&T`, a `ref struct`, or a
// type made of them. Option and Result may hold one where a reference may
// be (a returned `Option<&T>`).
bool holds_ref(Checker *c, TypeId type) {
    const Ty *ty = type_get(c->types, type);
    if (ty->kind == TY_REF) {
        return true;
    }
    if (ty->kind == TY_ADT && sema_def(c->sema, (DefId){ty->data})->item->kind == ITEM_STRUCT &&
        sema_def(c->sema, (DefId){ty->data})->item->as.struct_.is_ref) {
        return true;
    }
    for (uint32_t i = 0; (ty->kind == TY_ADT || ty->kind == TY_TUPLE) && i < ty->arg_count; i++) {
        if (holds_ref(c, ty->args[i])) {
            return true;
        }
    }
    return false;
}

void check_storable(Checker *c, TypeId type, Span span, const char *where) {
    if (holds_ref(c, type)) {
        check_error(c, "E0702", span, "a reference cannot be stored in %s: store an owned "
                    "value, or declare a `ref struct`", where);
    }
}

// The type arguments of a container may not hold references; those of
// Option and Result may, and a `ref struct` may hold anything.
void check_instance_storable(Checker *c, TypeId type, Span span) {
    const Ty *ty = type_get(c->types, type);
    if (ty->kind != TY_ADT || ty->data == c->sema->option.id || ty->data == c->sema->result.id) {
        return;
    }
    const Item *item = sema_def(c->sema, (DefId){ty->data})->item;
    if (item->kind == ITEM_STRUCT && item->as.struct_.is_ref) {
        return;
    }
    for (uint32_t i = 0; i < ty->arg_count; i++) {
        check_storable(c, ty->args[i], span, "a container or a struct");
    }
}

static bool lends(Checker *c, const Param *param, TypeId type) {
    return holds_ref(c, type) || (param->mode == MODE_BORROW && !type_is_copy(c, type));
}

// A function returning a reference borrows it from `self`, or else from its
// only borrowed parameter that can lend one (spec 5.6).
void check_borrow_source(Checker *c, FnDecl *fn, const Ty *sig, TypeId self_type) {
    if (!holds_ref(c, sig->inner)) {
        return;
    }
    fn->returns_borrow = true;
    if (fn->has_self && fn->self_mode != MODE_MOVE) {
        fn->borrow_source = -1;
        fn->source_by_ref = fn->self_mode == MODE_BORROW && !holds_ref(c, self_type);
        return;
    }
    if (fn->is_unsafe) {   // the caller guarantees what the reference points to
        fn->returns_borrow = false;
        return;
    }
    int32_t source = -1;
    uint32_t count = 0;
    for (uint32_t i = 0; i < fn->params.len && i < sig->arg_count; i++) {
        if (lends(c, &fn->params.items[i], sig->args[i])) {
            source = (int32_t)i;
            count++;
        }
    }
    if (count != 1) {
        check_error(c, "E0702", fn->span, "a function that returns a reference must borrow it "
                    "from `self` or from its only borrowed parameter, which it has %s",
                    count ? "several of" : "none of");
        fn->returns_borrow = false;
    }
    fn->borrow_source = source;
    fn->source_by_ref = count == 1 && fn->params.items[source].mode == MODE_BORROW &&
                        !holds_ref(c, sig->args[source]);
}

// Whether `local` is the parameter the function borrows from.
static bool is_source(const FnDecl *fn, uint32_t local) {
    if (fn->borrow_source < 0) {
        return fn->has_self && local == 0;
    }
    return fn->params.items[fn->borrow_source].local.id == local;
}

static void returned_ref(Checker *c, const Expr *value);

// The values a `match`, an `if` or a block gives: `return match x { .A(v) => &v }`
// returns a reference to the binding `v`, a copy that ends with the function.
static bool returned_branches(Checker *c, const Expr *value) {
    if (value->kind == EXPR_MATCH) {
        for (uint32_t i = 0; i < value->as.match.arms.len; i++) {
            returned_ref(c, value->as.match.arms.items[i].body);
        }
        return true;
    }
    if (value->kind == EXPR_IF) {
        returned_ref(c, &(Expr){.kind = EXPR_BLOCK, .as.block = value->as.if_.then});
        if (value->as.if_.else_) {
            returned_ref(c, value->as.if_.else_);
        }
        return true;
    }
    if (value->kind == EXPR_BLOCK || value->kind == EXPR_UNSAFE) {
        StmtList stmts = value->as.block->stmts;
        if (stmts.len > 0 && stmts.items[stmts.len - 1]->kind == STMT_EXPR) {
            returned_ref(c, stmts.items[stmts.len - 1]->as.expr);
        }
        return true;
    }
    return false;
}

// Whether `base[i]` reads a byte of a str, which the index copies out.
static bool indexes_str(Checker *c, const Expr *base) {
    TypeId type = base->type;
    while (type_get(c->types, type)->kind == TY_REF) {
        type = type_get(c->types, type)->inner;
    }
    return type.id == TID_STR;
}

// Whether `e` names a place: a binding, `self`, a dereference, or a field
// or an element of one.
static bool is_place_form(const Expr *e) {
    bool deref = e->kind == EXPR_UNARY && e->as.unary.op == OP_DEREF;
    return deref || e->kind == EXPR_IDENT || e->kind == EXPR_SELF || e->kind == EXPR_FIELD ||
           e->kind == EXPR_TUPLE_FIELD || e->kind == EXPR_INDEX;
}

// Whether `&e` borrows a temporary, which ends with the statement: the
// place reaches no binding, dereference or view (a reference, a pointer or
// a `ref struct`) that outlives it.
static bool borrows_temporary(Checker *c, const Expr *e) {
    if (!is_place_form(e) || (e->kind == EXPR_INDEX && indexes_str(c, e->as.index.base))) {
        return true;   // a new value, also a view or a byte of a str
    }
    for (;;) {
        const Ty *ty = type_get(c->types, e->type);
        if (ty->kind == TY_PTR || ty->kind == TY_ERROR || holds_ref(c, e->type)) {
            return false;
        }
        if (e->kind == EXPR_FIELD) {
            e = e->as.field.base;
        } else if (e->kind == EXPR_TUPLE_FIELD) {
            e = e->as.tuple_field.base;
        } else if (e->kind == EXPR_INDEX) {
            e = e->as.index.base;
        } else {
            bool deref = e->kind == EXPR_UNARY && e->as.unary.op == OP_DEREF;
            return !(e->kind == EXPR_IDENT || e->kind == EXPR_SELF || deref);
        }
    }
}

static void temporary_error(Checker *c, const Expr *value) {
    check_error(c, "E0702", value->span, "cannot return a reference to a temporary value, "
                "which ends with the function");
}

// `&v` for a binding `v` of a branch (only the binding itself: `&v[i]` may
// reach the elements of the matched value, which outlive the function).
static void returned_binding(Checker *c, const Expr *value) {
    const Expr *operand = value->as.unary.operand;
    if (borrows_temporary(c, operand)) {
        temporary_error(c, value);
        return;
    }
    bool local = (operand->kind == EXPR_IDENT || operand->kind == EXPR_SELF) &&
                 operand->as.ident.res.kind == RES_LOCAL;
    if (local && !is_source(c->fn, operand->as.ident.res.index) &&
        !holds_ref(c, c->local_types[operand->as.ident.res.index])) {
        check_error(c, "E0702", value->span, "cannot return a reference to `" SV_FMT "`, which "
                    "ends with the function",
                    SV_ARG(symbol_name(&c->sema->session->names,
                                       c->fn->locals.items[operand->as.ident.res.index].name)));
    }
}

static void returned_ref(Checker *c, const Expr *value) {
    if (!returned_branches(c, value) && value->kind == EXPR_UNARY &&
        (value->as.unary.op == OP_REF || value->as.unary.op == OP_REF_MUT)) {
        returned_binding(c, value);
    }
}

// `return &x` where `x` ends with the function, also inside `Some(&x)`.
void check_returned_ref(Checker *c, const Expr *value) {
    if (!c->fn->returns_borrow) {
        return;   // no reference returned, or E0702 on the signature already
    }
    if (returned_branches(c, value)) {
        return;
    }
    if (value->kind == EXPR_CALL && value->as.call.args.len == 1) {
        value = value->as.call.args.items[0].value;   // `Some(&x)`, `Ok(&x)`
    }
    if (value->kind != EXPR_UNARY || (value->as.unary.op != OP_REF &&
                                      value->as.unary.op != OP_REF_MUT)) {
        return;
    }
    if (borrows_temporary(c, value->as.unary.operand)) {
        temporary_error(c, value);
        return;
    }
    Expr *root = place_root(value->as.unary.operand);
    bool local = root && (root->kind == EXPR_IDENT || root->kind == EXPR_SELF) &&
                 root->as.ident.res.kind == RES_LOCAL;
    if (local && !is_source(c->fn, root->as.ident.res.index) &&
        !holds_ref(c, c->local_types[root->as.ident.res.index])) {
        check_error(c, "E0702", value->span, "cannot return a reference to `" SV_FMT "`, which "
                    "ends with the function",
                    SV_ARG(symbol_name(&c->sema->session->names,
                                       c->fn->locals.items[root->as.ident.res.index].name)));
    }
}
