// `.clone()` (spec 5.1, roadmap 3.3). A type that declares a `clone` method
// is cloned by it; the others get one derived by the compiler when all
// their parts can be cloned: Copy types are copied, str, List and Box are
// cloned by their std methods, and structs, enums and tuples part by part
// (air/lower_clone.c). A type with a `drop` method but no `clone` owns a
// resource the compiler cannot duplicate, so it cannot be cloned.

#include "sema/builtins.h"
#include "sema/check_internal.h"

#include <string.h>

#define MAX_DEPTH 64

static FnDecl *method_named(Checker *c, const Def *def, const char *name) {
    if (!def || !def->item) {
        return NULL;   // GCOVR_EXCL_LINE: every struct and enum declaration has its item
    }
    const Item *item = def->item;
    FnList methods = item->kind == ITEM_ENUM ? item->as.enum_.methods : item->as.struct_.methods;
    for (uint32_t i = 0; i < methods.len; i++) {
        if (sv_eq(symbol_name(&c->sema->session->names, methods.items[i]->name),
                  sv_from_cstr(name))) {
            return methods.items[i];
        }
    }
    return NULL;
}

static bool in_std(Checker *c, const Def *def) {
    StrView module = c->sema->program.items[def->module].name;
    return module.len >= 3 && memcmp(module.ptr, "std", 3) == 0 &&
           (module.len == 3 || module.ptr[3] == '.');
}

typedef struct {
    TypeId items[MAX_DEPTH];
    uint32_t len;
} Visiting;

static bool cloneable_in(Checker *c, TypeId type, Visiting *v);

static bool args_cloneable(Checker *c, TypeId type, Visiting *v) {
    const Ty *ty = type_get(c->types, type);
    for (uint32_t i = 0; i < ty->arg_count; i++) {
        if (!cloneable_in(c, type_get(c->types, type)->args[i], v)) {
            return false;
        }
    }
    return true;
}

static bool fields_cloneable(Checker *c, TypeId type, const Def *def, Visiting *v) {
    if (def->kind == DEF_STRUCT) {
        for (uint32_t i = 0; def->fields && i < def->fields->count; i++) {
            if (!cloneable_in(c, struct_field_type(c->sema, type, i), v)) {
                return false;
            }
        }
        return true;
    }
    for (uint32_t k = 0; def->variants && k < def->variants->count; k++) {
        for (uint32_t i = 0; i < def->variants->variants[k].count; i++) {
            if (!cloneable_in(c, variant_field_type(c->sema, type, k, i), v)) {
                return false;
            }
        }
    }
    return true;
}

// A std type's `clone` (List, Box) clones its elements; a user type's is
// its own business.
static bool adt_cloneable(Checker *c, TypeId type, Visiting *v) {
    const Def *def = sema_def(c->sema, (DefId){type_get(c->types, type)->data});
    if (method_named(c, def, "clone")) {
        return !in_std(c, def) || args_cloneable(c, type, v);
    }
    if (method_named(c, def, "drop")) {
        return false;
    }
    return fields_cloneable(c, type, def, v);
}

static bool cloneable_in(Checker *c, TypeId type, Visiting *v) {
    const Ty *ty = type_get(c->types, type);
    if (ty->kind == TY_PARAM) {
        return false;   // `Clone` is not a stage 0 bound (docs/bootstrap.md)
    }
    if (ty->kind == TY_STR || type_is_copy(c, type)) {
        return true;
    }
    if (ty->kind == TY_TUPLE) {
        return args_cloneable(c, type, v);
    }
    if (ty->kind != TY_ADT) {
        return false;
    }
    for (uint32_t i = 0; i < v->len; i++) {
        if (v->items[i].id == type.id) {
            return true;   // a recursive type: cloneable if the rest is
        }
    }
    if (v->len == MAX_DEPTH) {
        return false;
    }
    v->items[v->len++] = type;
    bool ok = adt_cloneable(c, type, v);
    v->len--;
    return ok;
}

bool type_is_cloneable(Checker *c, TypeId type) {
    Visiting v = {.len = 0};
    return cloneable_in(c, type, &v);
}

static void not_cloneable(Checker *c, Span span, TypeId type) {
    if (type.id != TID_ERROR) {
        check_error(c, "E0322", span, "type `%s` cannot be cloned: declare a `clone` method, "
                    "or give it only parts that can be cloned", type_name(c, type));
    }
}

// `value.clone()` on a type without a `clone` method: the derived clone.
TypeId check_derived_clone(Checker *c, Expr *call, TypeId self) {
    Expr *field = call->as.call.callee;
    if (call->as.call.args.len > 0 || field->as.field.type_args.len > 0) {
        check_error(c, "E0302", call->span, "`clone` takes no arguments");
    }
    for (uint32_t i = 0; i < call->as.call.args.len; i++) {
        check_expr(c, call->as.call.args.items[i].value, NO_TYPE);
    }
    if (!type_is_cloneable(c, self)) {
        not_cloneable(c, field->span, self);
        return type_id(TID_ERROR);
    }
    field->as.field.res = (Res){RES_BUILTIN, BM_CLONE};
    return self;
}

// `list.clone()` calls the std method, which clones each element: they
// must be cloneable too.
void check_std_clone(Checker *c, Expr *call, DefId owner, FnDecl *fn, TypeId self) {
    if (!in_std(c, sema_def(c->sema, owner)) || !fn->has_self ||
        !sv_eq(symbol_name(&c->sema->session->names, fn->name), SV("clone"))) {
        return;
    }
    if (!type_is_cloneable(c, self)) {
        not_cloneable(c, call->as.call.callee->span, self);
    }
}
