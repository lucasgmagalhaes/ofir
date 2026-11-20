// Lowering of `?` and `??` (spec 6.2 and 4.8): tests of the tag of an
// Option or a Result.

#include "air/lower_internal.h"

// The position of the variant `name` of the enum `type` (`Some`, `Err`).
static uint32_t variant_named(Lowerer *l, TypeId type, const char *name) {
    const EnumInfo *info = sema_def(l->sema, (DefId){type_get(l->types, type)->data})->variants;
    for (uint32_t i = 0; i < info->count; i++) {
        if (sv_eq(symbol_name(&l->sema->session->names, info->variants[i].name),
                  sv_from_cstr(name))) {
            return i;
        }
    }
    // GCOVR_EXCL_START: a std without the items the compiler calls
    fatal("lowering: std defines no variant `%s`", name);
    // GCOVR_EXCL_STOP
}

static bool is_option(Lowerer *l, TypeId type) {
    return type_get(l->types, type)->data == l->sema->option.id;
}

// `value?`: on `None` or `Err(e)`, returns `None` or `Err(e)` from the
// function; else continues with the content.
Operand lower_try(Lowerer *l, Expr *e) {
    TypeId type = e->as.inner->type;
    bool option = is_option(l, type);
    Place value = {.local = to_local(l, lower_expr(l, e->as.inner))};
    uint32_t good = variant_named(l, type, option ? "Some" : "Ok");
    uint32_t bad = variant_named(l, type, option ? "None" : "Err");
    BlockId fail = air_new_block(l->body), next = air_new_block(l->body);
    terminate(l, (Terminator){.kind = TERM_BRANCH, .cond = is_variant(l, value, type, good),
                              .target = next, .other = fail});
    switch_to(l, fail);
    TypeId ret = l->body->locals.data[0].type;
    TypeId error = type_get(l->types, type)->arg_count > 1 ? type_get(l->types, type)->args[1]
                                                           : type_id(TID_UNIT);
    Operand payload = place_operand(variant_field(l, value, bad, 0), OPERAND_COPY, error);
    Operand returned = variant_value(l, ret, variant_named(l, ret, option ? "None" : "Err"),
                                     &payload, option ? 0 : 1);
    emit_assign(l, (LocalId){0}, (Rvalue){.kind = RV_USE, .a = returned});
    drop_temps(l, 0);
    drop_scopes(l, 0);
    terminate(l, (Terminator){.kind = TERM_RETURN});
    switch_to(l, next);
    return place_operand(variant_field(l, value, good, 0), OPERAND_COPY, e->type);
}

// `a ?? b`: the content of `a` if it is `Some`, else `b`, evaluated only
// then. When `b` is an Option, the result is `a` itself or `b`.
Operand lower_coalesce(Lowerer *l, Expr *e) {
    Expr *lhs = e->as.binary.lhs;
    TypeId type = lhs->type;
    Place value = {.local = to_local(l, lower_expr(l, lhs))};
    uint32_t some = variant_named(l, type, "Some");
    LocalId dest = new_temp(l, e->type);
    BlockId then = air_new_block(l->body), other = air_new_block(l->body);
    BlockId join = air_new_block(l->body);
    terminate(l, (Terminator){.kind = TERM_BRANCH, .cond = is_variant(l, value, type, some),
                              .target = then, .other = other});
    switch_to(l, then);
    Operand content = type_eq(e->type, type)
                          ? place_operand(value, OPERAND_COPY, type)
                          : place_operand(variant_field(l, value, some, 0), OPERAND_COPY, e->type);
    emit_assign(l, dest, (Rvalue){.kind = RV_USE, .a = content});
    emit_goto(l, join);
    switch_to(l, other);
    temp_scope_enter(l);   // evaluated on one path only
    Operand fallback = lower_expr(l, e->as.binary.rhs);
    if (fallback.kind != OPERAND_UNIT) {   // a fallback that diverges has no value
        emit_assign(l, dest, (Rvalue){.kind = RV_USE, .a = fallback});
    }
    temp_scope_leave(l);
    emit_goto(l, join);
    switch_to(l, join);
    return op_copy(dest, e->type);
}
