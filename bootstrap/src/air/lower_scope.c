// Drop scopes and drop flags (spec 5.2): which locals own a value to drop,
// and the drops at the end of their block and at the jumps that leave it.

#include "air/lower_internal.h"

#include <string.h>

void drop_scope_enter(Lowerer *l) {
    VEC_PUSH(l->scopes, (uint32_t)l->owned.len);
}

static TypeId local_type(Lowerer *l, LocalId local) {
    return concrete_type(l, l->body->locals.data[local.id].type);
}

// The flag of a part moved out somewhere, or 0.
static LocalId part_flag(Lowerer *l, LocalId local, const uint32_t *path, uint32_t len) {
    for (size_t i = 0; i < l->part_flags.len; i++) {
        const MovedPart *p = &l->part_flags.data[i].part;
        if (p->local == local.id && p->len == len &&
            memcmp(p->fields, path, len * sizeof(uint32_t)) == 0) {
            return l->part_flags.data[i].flag;
        }
    }
    return (LocalId){0};
}

// Whether some moved part lies strictly inside `path`.
static bool part_inside(Lowerer *l, LocalId local, const uint32_t *path, uint32_t len) {
    for (size_t i = 0; i < l->part_flags.len; i++) {
        const MovedPart *p = &l->part_flags.data[i].part;
        if (p->local == local.id && p->len > len &&
            memcmp(p->fields, path, len * sizeof(uint32_t)) == 0) {
            return true;
        }
    }
    return false;
}

// Branches on `flag`: the code emitted until `if_done` runs only if set.
static BlockId if_set(Lowerer *l, LocalId flag) {
    BlockId then = air_new_block(l->body), next = air_new_block(l->body);
    terminate(l, (Terminator){.kind = TERM_BRANCH, .cond = op_copy(flag, type_id(TID_BOOL)),
                              .target = then, .other = next});
    switch_to(l, then);
    return next;
}

static void if_done(Lowerer *l, BlockId next) {
    emit_goto(l, next);
    switch_to(l, next);
}

static Place sub_place(Lowerer *l, Place base, uint32_t field) {
    uint32_t *fields = ARENA_ARRAY(&l->sema->session->arena, uint32_t, base.path.len + 1);
    for (uint32_t i = 0; i < base.path.len; i++) {
        fields[i] = base.path.fields[i];
    }
    fields[base.path.len] = field;
    base.path.fields = fields;
    base.path.len++;
    return base;
}

// Drops what is left of a partially moved value: each field, except the
// moved parts, whose flags decide (a struct or a tuple: the analysis only
// moves fields out of those).
// The value being taken apart: the local, the path from it so far, its depth.
typedef struct {
    LocalId local;
    uint32_t *path;
    uint32_t depth;
} Rest;

static void drop_rest(Lowerer *l, Rest rest, Place place, TypeId type) {
    LocalId local = rest.local;
    uint32_t depth = rest.depth, *path = rest.path;
    const Ty *ty = type_get(l->types, type);
    uint32_t count = ty->kind == TY_TUPLE ? ty->arg_count
                                          : sema_def(l->sema, (DefId){ty->data})->fields->count;
    for (uint32_t i = 0; i < count && depth < PART_DEPTH; i++) {
        path[depth] = i;
        TypeId field = type_get(l->types, type)->kind == TY_TUPLE
                           ? type_get(l->types, type)->args[i]
                           : struct_field_type(l->sema, type, i);
        Place at = sub_place(l, place, i);
        LocalId flag = part_flag(l, local, path, depth + 1);
        if (flag.id != 0) {
            BlockId next = if_set(l, flag);
            emit_drop(l, at, field);
            if_done(l, next);
        } else if (part_inside(l, local, path, depth + 1)) {
            drop_rest(l, (Rest){local, path, depth + 1}, at, field);
        } else {
            emit_drop(l, at, field);
        }
    }
}

// `if flag { drop(local) }`, part by part if parts may have been moved out.
static void drop_if_set(Lowerer *l, LocalId local) {
    BlockId next = if_set(l, l->flags[local.id]);
    Place place = {.local = local, .path = {.deref = l->body->locals.data[local.id].by_ref}};
    uint32_t path[PART_DEPTH];
    if (part_inside(l, local, path, 0)) {
        drop_rest(l, (Rest){local, path, 0}, place, local_type(l, local));
    } else {
        emit_drop(l, place, local_type(l, local));
    }
    if_done(l, next);
}

// Drops the locals owned by the scopes from `first` on, the latest first,
// without closing the scopes: for the jumps that leave them.
void drop_scopes(Lowerer *l, uint32_t first) {
    if (first >= l->scopes.len) {
        return;
    }
    for (size_t i = l->owned.len; i > l->scopes.data[first]; i--) {
        drop_if_set(l, l->owned.data[i - 1]);
    }
}

void drop_scope_leave(Lowerer *l) {
    drop_scopes(l, (uint32_t)l->scopes.len - 1);
    l->owned.len = VEC_LAST(l->scopes);
    l->scopes.len--;
}

static void set_flag_of(Lowerer *l, LocalId flag, bool value) {
    emit_assign(l, flag, (Rvalue){.kind = RV_USE, .a = op_bool(value)});
}

// Sets the local's flag and the flags of its parts: all have a value or none.
static void set_flag(Lowerer *l, LocalId local, bool value) {
    set_flag_of(l, l->flags[local.id], value);
    for (size_t i = 0; i < l->part_flags.len; i++) {
        if (l->part_flags.data[i].part.local == local.id) {
            set_flag_of(l, l->part_flags.data[i].flag, value);
        }
    }
}

// Flags for the parts of `local` that the move analysis saw moved out.
static void flag_parts(Lowerer *l, LocalId local) {
    const FnDecl *fn = l->fn;
    for (uint32_t i = 0; fn && i < fn->moved_part_count; i++) {
        MovedPart part = fn->moved_parts[i];
        part.local = air_local_of((LocalId){part.local}).id;
        if (part.local == local.id && part_flag(l, local, part.fields, part.len).id == 0) {
            VEC_PUSH(l->part_flags, ((PartFlag){part, new_temp(l, type_id(TID_BOOL))}));
        }
    }
}

void own_local(Lowerer *l, LocalId local, bool initialized) {
    if (local.id >= l->flag_count || is_glue(l->body->def) ||
        !needs_drop(l, local_type(l, local))) {
        return;
    }
    if (local.id > 0 && l->fn_locals && l->fn_locals[local.id - 1].borrows) {
        return;   // a `for` variable or a binding of a borrowed place
    }
    if (l->flags[local.id].id == 0) {
        l->flags[local.id] = new_temp(l, type_id(TID_BOOL));
        flag_parts(l, local);
    }
    set_flag(l, local, initialized);
    VEC_PUSH(l->owned, local);
}

void drop_moved(Lowerer *l, LocalId local) {
    if (local.id < l->flag_count && l->flags[local.id].id != 0) {
        set_flag(l, local, false);
    }
}

// Before `local = value`: the old value is dropped; after it, the flag is
// set (assign_done).
void drop_before_assign(Lowerer *l, LocalId local) {
    if (local.id < l->flag_count && l->flags[local.id].id != 0) {
        drop_if_set(l, local);
    }
}

void assign_done(Lowerer *l, LocalId local) {
    if (local.id < l->flag_count && l->flags[local.id].id != 0) {
        set_flag(l, local, true);
    }
}

static void own_list(Lowerer *l, PatternList list) {
    for (uint32_t i = 0; i < list.len; i++) {
        own_bindings(l, list.items[i]);
    }
}

void own_bindings(Lowerer *l, const Pattern *p) {
    switch (p->kind) {
    case PAT_BINDING: own_local(l, air_local_of(p->as.binding.local), true); break;
    case PAT_VARIANT: own_list(l, p->as.variant.args); break;
    case PAT_TUPLE: own_list(l, p->as.tuple); break;
    case PAT_TUPLE_STRUCT: own_list(l, p->as.tuple_struct.args); break;
    case PAT_OR: own_bindings(l, p->as.alternatives.items[0]); break;   // same locals
    case PAT_STRUCT:
        for (uint32_t i = 0; i < p->as.struct_.fields.len; i++) {
            own_bindings(l, p->as.struct_.fields.items[i].pattern);
        }
        break;
    default: break;
    }
}

// The local and field path of a place expression; false if not a local's.
bool place_path(const Expr *e, LocalId *local, uint32_t *path, uint32_t *len) {
    uint32_t reversed[PART_DEPTH], depth = 0;
    while (e->kind == EXPR_FIELD || e->kind == EXPR_TUPLE_FIELD) {
        if (depth < PART_DEPTH) {
            reversed[depth++] = e->kind == EXPR_FIELD ? e->as.field.res.index
                                                      : e->as.tuple_field.index;
        }
        e = e->kind == EXPR_FIELD ? e->as.field.base : e->as.tuple_field.base;
    }
    if ((e->kind != EXPR_IDENT && e->kind != EXPR_SELF) || e->as.ident.res.kind != RES_LOCAL) {
        return false;
    }
    *local = air_local_of((LocalId){e->as.ident.res.index});
    *len = depth;
    for (uint32_t i = 0; i < depth; i++) {
        path[i] = reversed[depth - 1 - i];
    }
    return true;
}

// The value of a place is moved out: the flag of the part, or of the
// whole local, is cleared.
void note_moved(Lowerer *l, const Expr *e) {
    LocalId local;
    uint32_t path[PART_DEPTH], len;
    if (!place_path(e, &local, path, &len)) {
        return;
    }
    LocalId flag = len > 0 ? part_flag(l, local, path, len) : (LocalId){0};
    if (flag.id != 0) {
        set_flag_of(l, flag, false);
    } else {
        drop_moved(l, local);
    }
}

// `p.a = v` for a part that may have been moved out: dropped only if its
// flag is set, which the assignment then sets. False if it has no flag.
bool assign_part(Lowerer *l, const Expr *target, Place place, TypeId type) {
    LocalId local;
    uint32_t path[PART_DEPTH], len;
    if (!place_path(target, &local, path, &len) || len == 0) {
        return false;
    }
    LocalId flag = part_flag(l, local, path, len);
    if (flag.id == 0) {
        return false;
    }
    BlockId next = if_set(l, flag);
    emit_drop(l, place, type);
    if_done(l, next);
    set_flag_of(l, flag, true);
    return true;
}

// ---------------------------------------------------------------------------
// Temporaries

void temp_scope_enter(Lowerer *l) {
    VEC_PUSH(l->temp_scopes, (uint32_t)l->temps.len);
}

void drop_temps(Lowerer *l, uint32_t first) {
    if (first >= l->temp_scopes.len) {
        return;
    }
    for (size_t i = l->temps.len; i > l->temp_scopes.data[first]; i--) {
        Temp t = l->temps.data[i - 1];
        emit_drop(l, (Place){.local = t.local}, t.type);
    }
}

void temp_scope_leave(Lowerer *l) {
    drop_temps(l, (uint32_t)l->temp_scopes.len - 1);
    l->temps.len = VEC_LAST(l->temp_scopes);
    l->temp_scopes.len--;
}

// The expressions that make a new value (and those that pass on the value
// of their last expression: `if`, `match`, blocks).
static bool makes_value(const Expr *e) {
    switch (e->kind) {
    case EXPR_CALL: case EXPR_INTERP: case EXPR_STRUCT: case EXPR_ARRAY:
    case EXPR_ARRAY_REPEAT: case EXPR_TUPLE: case EXPR_VARIANT: case EXPR_TRY:
    case EXPR_IF: case EXPR_MATCH: case EXPR_BLOCK: case EXPR_UNSAFE:
        return true;
    case EXPR_BINARY: return e->as.binary.op == OP_ADD || e->as.binary.op == OP_COALESCE;
    case EXPR_FIELD: return e->as.field.res.kind == RES_VARIANT;
    default: return false;
    }
}

Operand own_temp(Lowerer *l, const Expr *e, Operand value) {
    bool forwarded = l->forward;
    l->forward = false;
    if (forwarded || e->consumed || !makes_value(e) || l->temp_scopes.len == 0 ||
        is_glue(l->body->def) || value.kind == OPERAND_UNIT) {
        return value;
    }
    TypeId type = concrete_type(l, e->type);
    if (!needs_drop(l, type)) {
        return value;
    }
    LocalId local = to_local(l, value);
    VEC_PUSH(l->temps, ((Temp){local, type}));
    return op_copy(local, value.type);
}
