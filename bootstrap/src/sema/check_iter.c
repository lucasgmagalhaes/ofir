// `for x in iterator` (roadmap 3.7). Until interfaces (roadmap 5.8), a
// value is an iterator when its type declares `fn next(mut self)
// Option<T>`: the loop calls `next` until it returns `None`, and `x` owns
// each item. An iterator held in a variable is advanced in place, so the
// variable must be a `let`; the loop leaves it where it stopped.

#include "sema/check_internal.h"

static const FnDecl *next_method(Checker *c, const Def *def) {
    if (!def || !def->item || (def->kind != DEF_STRUCT && def->kind != DEF_ENUM)) {
        return NULL;   // GCOVR_EXCL_LINE: every ADT is a struct or enum declared by an item
    }
    const Item *item = def->item;
    FnList methods = item->kind == ITEM_ENUM ? item->as.enum_.methods : item->as.struct_.methods;
    for (uint32_t i = 0; i < methods.len; i++) {
        const FnDecl *fn = methods.items[i];
        if (fn->has_self && fn->self_mode == MODE_MUT && fn->params.len == 0 &&
            sv_eq(symbol_name(&c->sema->session->names, fn->name), SV("next"))) {
            return fn;
        }
    }
    return NULL;
}

bool iterator_item(Checker *c, TypeId type, TypeId *item) {
    const Ty *ty = type_get(c->types, type);
    if (ty->kind != TY_ADT || c->sema->option.id == 0) {
        return false;
    }
    const FnDecl *next = next_method(c, sema_def(c->sema, (DefId){ty->data}));
    if (!next || next->type_params.len > 0) {   // the owner's are the type's arguments
        return false;
    }
    TypeId ret = type_get(c->types, fn_signature(c, (FnDecl *)next))->inner;
    ty = type_get(c->types, type);
    ret = type_subst(c->types, ret, ty->args, ty->arg_count);
    const Ty *option = type_get(c->types, ret);
    if (option->kind != TY_ADT || option->data != c->sema->option.id) {
        return false;
    }
    *item = option->args[0];
    return true;
}

// The item type of `for x in it`; `it` is advanced in place when it is a
// variable or a field.
TypeId check_for_iterator(Checker *c, Stmt *s, TypeId item) {
    Expr *expr = s->as.for_.expr;
    if (s->as.for_.second.id != 0) {
        check_error(c, "E0300", expr->span, "an iterator gives one value at a time: "
                    "`for item in iterator`");
    }
    if (place_root(expr)) {
        require_mutable(c, expr);
    }
    return item;
}
