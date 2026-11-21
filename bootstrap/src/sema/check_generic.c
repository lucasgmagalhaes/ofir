// Type arguments of generic calls, struct literals and variants (spec 8.5).
//
// The arguments come from what is written (`f<int>(x)`), then from the
// expected type, then from the value arguments, left to right: the first
// type found for a parameter is its argument, and the values are then
// checked against the substituted signature. In a signature, TY_PARAM `i`
// always means the callee's parameter `i`.

#include "sema/check_internal.h"

static StrView name_of(Checker *c, Symbol name) {
    return symbol_name(&c->sema->session->names, name);
}

Generics fn_generics(const Def *fn) {
    return (Generics){.owner = fn->item ? item_type_params(fn->item) : (SymbolList){0},
                      .owner_bounds = fn->item ? fn->item->bounds : (WhereList){0},
                      .fn = fn->fn->type_params, .fn_bounds = fn->fn->bounds};
}

Infer infer_new(Checker *c, Generics params) {
    Arena *arena = &c->sema->session->arena;
    uint32_t count = params.owner.len + params.fn.len;
    return (Infer){.params = params, .count = count,
                   .args = ARENA_ARRAY(arena, TypeId, count + 1),
                   .known = ARENA_ARRAY(arena, bool, count + 1)};
}

void infer_explicit(Checker *c, Infer *inf, bool of_owner, TypeList list, Span span) {
    uint32_t first = of_owner ? 0 : inf->params.owner.len;
    uint32_t count = of_owner ? inf->params.owner.len : inf->params.fn.len;
    if (list.len != count) {
        check_error(c, "E0302", span, "wrong number of type arguments: expected %u, found %u",
                    count, list.len);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        inf->args[first + i] = resolve_type(c, list.items[i]);
        inf->known[first + i] = true;
    }
}

void infer_from_self(Infer *inf, const Ty *self, uint32_t count) {
    for (uint32_t i = 0; self->kind == TY_ADT && i < count && i < self->arg_count; i++) {
        inf->args[i] = self->args[i];
        inf->known[i] = true;
    }
}

static bool has_inner(TyKind kind) {
    return kind == TY_REF || kind == TY_PTR || kind == TY_SLICE || kind == TY_ARRAY ||
           kind == TY_FN;
}

void infer_from(Checker *c, Infer *inf, TypeId pattern, TypeId actual) {
    const Ty *p = type_get(c->types, pattern);
    if (p->has_param && actual.id == TID_ERROR) {
        inf->poisoned = true;
    }
    if (!p->has_param || actual.id == TID_ERROR || actual.id == TID_NEVER) {
        return;
    }
    if (p->kind == TY_PARAM) {
        if (p->data < inf->count && !inf->known[p->data]) {
            inf->args[p->data] = actual;
            inf->known[p->data] = true;
        }
        return;
    }
    const Ty *a = type_get(c->types, actual);
    if (a->kind != p->kind || a->arg_count != p->arg_count ||
        (p->kind == TY_ADT && a->data != p->data)) {
        return;   // a mismatch, reported when the argument is checked
    }
    if (has_inner(p->kind)) {
        infer_from(c, inf, p->inner, a->inner);
    }
    for (uint32_t i = 0; i < p->arg_count; i++) {
        infer_from(c, inf, p->args[i], a->args[i]);
    }
}

static bool all_known(Checker *c, const Infer *inf, TypeId pattern) {
    const Ty *p = type_get(c->types, pattern);
    if (!p->has_param) {
        return true;
    }
    if (p->kind == TY_PARAM) {
        return p->data >= inf->count || inf->known[p->data];
    }
    if (has_inner(p->kind) && !all_known(c, inf, p->inner)) {
        return false;
    }
    for (uint32_t i = 0; i < p->arg_count; i++) {
        if (!all_known(c, inf, p->args[i])) {
            return false;
        }
    }
    return true;
}

TypeId infer_hint(Checker *c, const Infer *inf, TypeId pattern) {
    return all_known(c, inf, pattern) ? infer_apply(c, inf, pattern) : NO_TYPE;
}

TypeId infer_apply(Checker *c, const Infer *inf, TypeId pattern) {
    return type_subst(c->types, pattern, inf->args, inf->count);
}

void infer_finish(Checker *c, Infer *inf, Span span) {
    for (uint32_t i = 0; i < inf->count; i++) {
        if (inf->known[i]) {
            continue;
        }
        uint32_t owner = inf->params.owner.len;
        Symbol name = i < owner ? inf->params.owner.items[i] : inf->params.fn.items[i - owner];
        if (!inf->poisoned) {
            check_error(c, "E0330", span, "cannot infer the type parameter `" SV_FMT "`: write the "
                        "type arguments, as in `f<int>(x)`", SV_ARG(name_of(c, name)));
        }
        inf->args[i] = type_id(TID_ERROR);
        inf->known[i] = true;
    }
    check_bounds(c, inf->params, inf->args, inf->count, span);
}

TypeId check_generic_args(Checker *c, Expr *call, const Ty *signature, Infer *inf,
                          TypeId expected) {
    Ty sig = *signature;   // a copy: interning may move the type table
    ArgList args = call->as.call.args;
    if (args.len != sig.arg_count) {
        check_error(c, "E0302", call->span,
                    "wrong number of arguments: expected %u, found %u", sig.arg_count, args.len);
    }
    infer_from(c, inf, sig.inner, expected);
    for (uint32_t i = 0; i < args.len; i++) {
        Expr *arg = args.items[i].value;
        if (args.items[i].name.id != 0) {
            unsupported(c, arg->span, "named arguments are");
        }
        TypeId param = i < sig.arg_count ? sig.args[i] : NO_TYPE;
        TypeId found = check_expr(c, arg, i < sig.arg_count ? infer_hint(c, inf, param) : NO_TYPE);
        if (deref_arg(c, &args.items[i].value, param, true)) {
            found = args.items[i].value->type;
        }
        infer_from(c, inf, param, found);
    }
    infer_finish(c, inf, call->span);
    for (uint32_t i = 0; i < args.len && i < sig.arg_count; i++) {
        Expr *arg = args.items[i].value;
        require_type(c, arg, arg->type, infer_apply(c, inf, sig.args[i]));
    }
    call->as.call.type_args = type_tuple(c->types, inf->args, inf->count);
    return infer_apply(c, inf, sig.inner);
}
