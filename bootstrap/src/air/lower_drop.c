// Drops (spec 5.2, roadmap 3.2): drop glue, drop flags and scope exits.
//
// A type needs drop when dropping it does something: a type with a `drop`
// method (str, List and Box in std, and user types), or one with a field
// that needs drop. Its *drop glue* is a function `drop<T>(p: *mut T)`,
// lowered once per concrete type like an instance: it calls `drop`, then
// drops the fields (of the active variant, for an enum).
//
// A local that owns a value needing drop has a *drop flag*, a bool set when
// it gets a value and cleared when the value is moved out. At the end of
// its block, and at a `return`, `break` or `continue` that leaves the
// block, the value is dropped if the flag is set; LLVM removes the flags
// whose value it knows.

#include "air/lower_internal.h"

TypeId concrete_type(Lowerer *l, TypeId type) {
    const Ty *targs = type_get(l->types, l->body->targs);
    if (is_glue(l->body->def)) {
        // GCOVR_EXCL_START: glue bodies hold only drops and calls, which take no type
        return type;   // glue is lowered for a concrete type
        // GCOVR_EXCL_STOP
    }
    return type_subst(l->types, type, targs->args, targs->arg_count);
}

// The method `name` (with `self`) declared by the type `type`, or NULL.
FnDecl *type_method(Lowerer *l, TypeId type, StrView name) {
    const Ty *ty = type_get(l->types, type);
    const Def *def = ty->kind == TY_ADT ? sema_def(l->sema, (DefId){ty->data})
                                        : lang_def(l->sema, type);
    if (!def || !def->item) {
        return NULL;
    }
    const Item *item = def->item;
    FnList methods = item->kind == ITEM_ENUM ? item->as.enum_.methods : item->as.struct_.methods;
    for (uint32_t i = 0; i < methods.len; i++) {
        if (methods.items[i]->has_self &&
            sv_eq(symbol_name(&l->sema->session->names, methods.items[i]->name), name)) {
            return methods.items[i];
        }
    }
    return NULL;
}

static bool fields_need_drop(Lowerer *l, TypeId type) {
    const Ty *ty = type_get(l->types, type);
    if (ty->kind == TY_TUPLE) {
        for (uint32_t i = 0; i < ty->arg_count; i++) {
            if (needs_drop(l, type_get(l->types, type)->args[i])) {
                return true;
            }
        }
        return false;
    }
    if (ty->kind != TY_ADT) {
        return false;
    }
    const Def *def = sema_def(l->sema, (DefId){ty->data});
    if (def->kind == DEF_STRUCT) {
        for (uint32_t i = 0; def->fields && i < def->fields->count; i++) {
            if (needs_drop(l, struct_field_type(l->sema, type, i))) {
                return true;
            }
        }
        return false;
    }
    for (uint32_t v = 0; def->variants && v < def->variants->count; v++) {
        for (uint32_t i = 0; i < def->variants->variants[v].count; i++) {
            if (needs_drop(l, variant_field_type(l->sema, type, v, i))) {
                return true;
            }
        }
    }
    return false;
}

static bool compute_needs_drop(Lowerer *l, TypeId type) {
    const Ty *ty = type_get(l->types, type);
    if (ty->kind == TY_REF || ty->kind == TY_PTR || ty->kind == TY_PARAM ||
        ty->kind == TY_INT || ty->kind == TY_FLOAT || ty->kind == TY_BOOL) {
        return false;
    }
    return type_method(l, type, SV("drop")) != NULL || fields_need_drop(l, type);
}

// Cached by TypeId: asked for every new value and every local.
bool needs_drop(Lowerer *l, TypeId type) {
    while (l->drop_cache.len <= type.id) {
        VEC_PUSH(l->drop_cache, 0);
    }
    if (l->drop_cache.data[type.id] == 0) {
        l->drop_cache.data[type.id] = 3;   // in progress: no drop through itself
        l->drop_cache.data[type.id] = compute_needs_drop(l, type) ? 2 : 1;
    }
    return l->drop_cache.data[type.id] == 2;
}

// Drops the value at `place`, of the concrete type `type`.
void emit_drop(Lowerer *l, Place place, TypeId type) {
    if (!needs_drop(l, type)) {
        return;
    }
    Operand *arg = ARENA_NEW(&l->sema->session->arena, Operand);
    *arg = place_operand(place, OPERAND_ADDR, type_ref(l->types, type, true));
    TypeId targs = type_tuple(l->types, &type, 1);
    emit_direct_call(l, &(FnDecl){.def = {0}}, targs, (CallArgs){arg, 1}, NULL);
}

// The type arguments of a method of `type`: those of the type.
TypeId method_targs(Lowerer *l, TypeId type) {
    const Ty *ty = type_get(l->types, type);
    return ty->kind == TY_ADT ? type_tuple(l->types, ty->args, ty->arg_count) : type_id(TID_UNIT);
}

// ---------------------------------------------------------------------------
// Drop glue

static Place field_place(Lowerer *l, Place base, uint32_t step, uint32_t field) {
    uint32_t len = base.path.len + (step ? 2 : 1);
    uint32_t *fields = ARENA_ARRAY(&l->sema->session->arena, uint32_t, len);
    for (uint32_t i = 0; i < base.path.len; i++) {
        // GCOVR_EXCL_START: glue drops fields of the dereferenced parameter, an empty path
        fields[i] = base.path.fields[i];
        // GCOVR_EXCL_STOP
    }
    if (step) {
        fields[base.path.len] = step;
    }
    fields[len - 1] = field;
    base.path.fields = fields;
    base.path.len = len;
    return base;
}

static void drop_variants(Lowerer *l, Place value, TypeId type) {
    const Def *def = sema_def(l->sema, (DefId){type_get(l->types, type)->data});
    for (uint32_t v = 0; v < def->variants->count; v++) {
        uint32_t count = def->variants->variants[v].count;
        bool any = false;
        for (uint32_t i = 0; i < count; i++) {
            any = any || needs_drop(l, variant_field_type(l->sema, type, v, i));
        }
        if (!any) {
            continue;
        }
        BlockId drop = air_new_block(l->body), next = air_new_block(l->body);
        terminate(l, (Terminator){.kind = TERM_BRANCH, .cond = is_variant(l, value, type, v),
                                  .target = drop, .other = next});
        switch_to(l, drop);
        for (uint32_t i = 0; i < count; i++) {
            emit_drop(l, field_place(l, value, PATH_VARIANT | v, i),
                      variant_field_type(l->sema, type, v, i));
        }
        emit_goto(l, next);
        switch_to(l, next);
    }
}

// The body of `drop<T>(p)`: T's `drop` method, then its fields.
static void glue_body(Lowerer *l, TypeId type) {
    Place value = {.local = {1}, .path = {.deref = true}};
    FnDecl *method = type_method(l, type, SV("drop"));
    if (method) {
        TypeId targs = method_targs(l, type);
        Operand *self = ARENA_NEW(&l->sema->session->arena, Operand);
        *self = op_copy((LocalId){1}, type_ref(l->types, type, true));
        emit_direct_call(l, method, targs, (CallArgs){self, 1}, NULL);
    }
    const Ty *ty = type_get(l->types, type);
    if (ty->kind == TY_TUPLE || (ty->kind == TY_ADT &&
                                 sema_def(l->sema, (DefId){ty->data})->kind == DEF_STRUCT)) {
        uint32_t count = ty->kind == TY_TUPLE
                             ? ty->arg_count
                             : sema_def(l->sema, (DefId){ty->data})->fields->count;
        for (uint32_t i = 0; i < count; i++) {
            TypeId field = type_get(l->types, type)->kind == TY_TUPLE
                               ? type_get(l->types, type)->args[i]
                               : struct_field_type(l->sema, type, i);
            emit_drop(l, field_place(l, value, 0, i), field);
        }
    } else if (ty->kind == TY_ADT) {
        drop_variants(l, value, type);
    }
}

void lower_glue(Lowerer *l, TypeId targs, BodySink sink, void *context) {
    TypeId type = type_get(l->types, targs)->args[0];
    AirBody body = {.def = {0}, .targs = targs, .param_count = 1};
    l->body = &body;
    air_new_local(&body, type_id(TID_UNIT), (Symbol){0}, (Span){0});
    body.locals.data[0].type = type_id(TID_UNIT);
    air_new_local(&body, type_ref(l->types, type, true), (Symbol){0}, (Span){0});
    l->block = air_new_block(&body);
    glue_body(l, type);
    air_terminate(&body, l->block, (Terminator){.kind = TERM_RETURN});
    mono_body(l, &body);
    sink(context, &body);
    air_body_free(&body);
}

// `dropInPlace<T>(p)` and `forget<T>(value)` (std/core/list.of, box.of).
Operand lower_drop_intrinsic(Lowerer *l, Expr *call, bool forget) {
    Expr *arg = call->as.call.args.items[0].value;
    Operand value = lower_expr(l, arg);
    if (forget) {
        return op_unit();   // the value is consumed, and nothing runs
    }
    TypeId type = concrete_type(l, type_get(l->types, call->as.call.type_args)->args[0]);
    Place place = {.local = to_local(l, value), .path = {.deref = true}};
    emit_drop(l, place, type);
    return op_unit();
}
