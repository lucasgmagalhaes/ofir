// `Option<T>` and `Result<T, E>` in the compiler (spec 6): the `?` and `??`
// operators and unused results. The types themselves are in
// std/core/option.of and std/core/result.at.

#include "sema/check_internal.h"

#include <stdio.h>

static bool instance_of(Checker *c, TypeId type, DefId id, uint32_t count) {
    const Ty *ty = type_get(c->types, type);
    return id.id != 0 && ty->kind == TY_ADT && ty->data == id.id && ty->arg_count == count;
}

bool option_inner(Checker *c, TypeId type, TypeId *value) {
    if (!instance_of(c, type, c->sema->option, 1)) {
        return false;
    }
    *value = type_get(c->types, type)->args[0];
    return true;
}

bool result_parts(Checker *c, TypeId type, TypeId *value, TypeId *error) {
    if (!instance_of(c, type, c->sema->result, 2)) {
        return false;
    }
    *value = type_get(c->types, type)->args[0];
    *error = type_get(c->types, type)->args[1];
    return true;
}

// `?` on a Result: the function returns a Result with the same error type
// (stage 0 does not convert errors).
static void try_result(Checker *c, Expr *e, TypeId error) {
    TypeId value, returned;
    if (!result_parts(c, c->ret, &value, &returned)) {
        if (c->ret.id != TID_ERROR) {
            check_error(c, "E0300", e->span, "`?` on a Result needs the function to return a "
                        "Result, not `%s`", type_name(c, c->ret));
        }
        return;
    }
    if (!types_compatible(error, returned)) {
        char name[64];
        snprintf(name, sizeof name, "%s", type_name(c, error));
        check_error(c, "E0300", e->span, "mismatched types: `?` returns the error `%s`, but the "
                    "function returns errors of type `%s`", name, type_name(c, returned));
    }
}

// `value?`: the content of `Some` or `Ok`; `None` or `Err(e)` is returned.
TypeId check_try(Checker *c, Expr *e) {
    TypeId type = check_expr(c, e->as.inner, NO_TYPE);
    TypeId value, error;
    if (option_inner(c, type, &value)) {
        TypeId ignored;
        if (!option_inner(c, c->ret, &ignored) && c->ret.id != TID_ERROR) {
            check_error(c, "E0300", e->span, "`?` on an Option needs the function to return an "
                        "Option, not `%s`", type_name(c, c->ret));
        }
        return value;
    }
    if (result_parts(c, type, &value, &error)) {
        try_result(c, e, error);
        return value;
    }
    if (type.id != TID_ERROR) {
        check_error(c, "E0313", e->span, "`?` needs an Option or a Result, not `%s`",
                    type_name(c, type));
    }
    return type_id(TID_ERROR);
}

// `a ?? b`: the content of `a`, else `b`, of the content's type; when `b`
// is an Option too, the result is an Option.
TypeId check_coalesce(Checker *c, Expr *e, TypeId expected) {
    Expr *lhs = e->as.binary.lhs, *rhs = e->as.binary.rhs;
    TypeId hint = NO_TYPE;
    if (expected.id != TID_ERROR && c->sema->option.id != 0) {
        hint = type_intern(c->types, (Ty){.kind = TY_ADT, .data = c->sema->option.id,
                                          .arg_count = 1, .args = &expected});
    }
    TypeId type = check_expr(c, lhs, option_inner(c, expected, &(TypeId){0}) ? expected : hint);
    TypeId value;
    if (!option_inner(c, type, &value)) {
        if (type.id != TID_ERROR) {
            check_error(c, "E0313", lhs->span, "`??` needs an Option on its left, not `%s`",
                        type_name(c, type));
        }
        check_expr(c, rhs, NO_TYPE);
        return type_id(TID_ERROR);
    }
    TypeId other = check_expr(c, rhs, value);
    if (type_eq(other, type)) {
        return type;   // `a ?? b ?? c` where `b` is an Option
    }
    require_type(c, rhs, other, value);
    return value;
}

static bool variant_is(Checker *c, const VariantInfo *v, const char *name, uint32_t count) {
    return sv_eq(symbol_name(&c->sema->session->names, v->name), sv_from_cstr(name)) &&
           v->count == count;
}

// The compiler builds and tests these variants by position: `Some(T)`
// then `None`, and `Ok(T)` then `Err(E)`.
void check_wrapper_layout(Checker *c, DefId id) {
    const Def *def = sema_def(c->sema, id);
    const EnumInfo *info = def->variants;
    bool option = id.id == c->sema->option.id;
    bool ok = info && info->count == 2 &&
              (option ? variant_is(c, &info->variants[0], "Some", 1) &&
                            variant_is(c, &info->variants[1], "None", 0)
                      : variant_is(c, &info->variants[0], "Ok", 1) &&
                            variant_is(c, &info->variants[1], "Err", 1));
    if (!ok) {
        // GCOVR_EXCL_START: only std declares `@lang` types, with these layouts
        check_error(c, "E0806", def->span, option ? "`@lang(\"option\")` must declare "
                    "`Some(T)` then `None`" : "`@lang(\"result\")` must declare `Ok(T)` then "
                    "`Err(E)`");
        // GCOVR_EXCL_STOP
    }
}

void check_unused_result(Checker *c, const Expr *e) {
    TypeId value, error;
    if (result_parts(c, e->type, &value, &error)) {
        check_error(c, "E0305", e->span, "unused `Result` that must be used: handle the error, "
                    "or ignore it explicitly with `const _ = ...`");
    }
}
