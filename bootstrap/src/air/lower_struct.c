// Lowering of struct literals, tuple struct constructors and field places.

#include "air/lower_internal.h"

// Collects the field positions of `e` from the innermost field outwards.
// Returns the root expression.
static bool is_deref(const Expr *e) {
    return e->kind == EXPR_UNARY && e->as.unary.op == OP_DEREF;
}

static Expr *collect_path(Expr *e, uint32_t *fields, uint32_t *len) {
    if (e->kind == EXPR_FIELD && e->as.field.res.kind == RES_FIELD) {
        Expr *root = collect_path(e->as.field.base, fields, len);
        fields[(*len)++] = e->as.field.res.index;
        return root;
    }
    if (e->kind == EXPR_TUPLE_FIELD) {
        Expr *root = collect_path(e->as.tuple_field.base, fields, len);
        fields[(*len)++] = e->as.tuple_field.index;
        return root;
    }
    return e;
}

static uint32_t path_depth(const Expr *e) {
    uint32_t depth = 0;
    while (e->kind == EXPR_TUPLE_FIELD ||
           (e->kind == EXPR_FIELD && e->as.field.res.kind == RES_FIELD)) {
        e = e->kind == EXPR_FIELD ? e->as.field.base : e->as.tuple_field.base;
        depth++;
    }
    return depth;
}

// The place of `e`: a local and a field path. A root that is not a variable,
// as in `make().x`, is evaluated into a temporary first.
Place lower_place(Lowerer *l, Expr *e) {
    uint32_t depth = path_depth(e);
    uint32_t *fields = ARENA_ARRAY(&l->sema->session->arena, uint32_t, depth + 1);
    uint32_t len = 0;
    Expr *root = collect_path(e, fields, &len);
    LocalId local;
    bool deref = false;
    if (is_deref(root)) {   // `*p`: the pointer's value is the address of the place
        local = to_local(l, lower_expr(l, root->as.unary.operand));
        deref = true;
    } else if (root->kind == EXPR_INDEX && is_list(l, root->as.index.base->type)) {
        local = lower_list_index(l, root).local;   // `l[i].x`: from the element's address
        deref = true;
    } else if ((root->kind == EXPR_IDENT || root->kind == EXPR_SELF) &&
               root->as.ident.res.kind == RES_LOCAL) {
        local = air_local_of((LocalId){root->as.ident.res.index});
        deref = l->body->locals.data[local.id].by_ref;
    } else if (root->kind == EXPR_IDENT && root->as.ident.res.kind == RES_DEF &&
               sema_def(l->sema, (DefId){root->as.ident.res.index})->kind == DEF_GLOBAL) {
        local = lower_global_addr(l, root->as.ident.res.index);   // `COUNTER` or `lib.COUNTER`
        deref = true;
    } else {
        local = to_local(l, lower_expr(l, root));
    }
    return (Place){.local = local, .path = {.fields = fields, .len = len, .deref = deref}};
}

// A temporary holding the address of the mutable global `def` (spec 11.5).
LocalId lower_global_addr(Lowerer *l, uint32_t def) {
    TypeId ty = sema_def(l->sema, (DefId){def})->type;
    TypeId ptr = type_intern(l->types, (Ty){.kind = TY_PTR, .data = 1, .inner = ty});
    return emit_temp(l, ptr, (Rvalue){.kind = RV_GLOBAL_ADDR, .def = def});
}

Operand place_operand(Place place, OperandKind kind, TypeId type) {
    return (Operand){.kind = kind, .type = type, .as.local = place.local, .path = place.path};
}

Operand lower_field_read(Lowerer *l, Expr *e) {
    return place_operand(lower_place(l, e), OPERAND_COPY, e->type);
}

static Operand aggregate(Lowerer *l, TypeId type, Operand *items, uint32_t count) {
    Rvalue rv = {.kind = RV_AGGREGATE, .items = items, .count = count};
    return op_copy(emit_temp(l, type, rv), type);
}

static uint32_t field_position(const StructInfo *info, Symbol name) {
    for (uint32_t f = 0; f < info->count; f++) {
        if (symbol_eq(info->names[f], name)) {
            return f;
        }
    }
    return 0;   // the checker reported unknown fields
}

// Field `f` of the `..base` struct.
static Operand base_field(Lowerer *l, LocalId base, uint32_t f, TypeId type) {
    uint32_t *field = ARENA_NEW(&l->sema->session->arena, uint32_t);
    *field = f;
    Place place = {.local = base, .path = {.fields = field, .len = 1}};
    return place_operand(place, OPERAND_COPY, type);
}

// Fields are evaluated in source order; omitted ones take their default or
// come from the `..base` struct.
Operand lower_struct_literal(Lowerer *l, Expr *e) {
    const Ty *ty = type_get(l->types, e->type);
    const StructInfo *info = sema_def(l->sema, (DefId){ty->data})->fields;
    Operand *items = ARENA_ARRAY(&l->sema->session->arena, Operand, info->count + 1);
    bool *given = ARENA_ARRAY(&l->sema->session->arena, bool, info->count + 1);
    uint32_t count = e->as.struct_.fields.len;
    for (uint32_t i = 0; i < count; i++) {
        FieldInit *init = &e->as.struct_.fields.items[i];
        uint32_t f = field_position(info, init->name);
        items[f] = lower_expr(l, init->value);
        if (i + 1 < count) {
            items[f] = settle(l, items[f], e->as.struct_.fields.items[i + 1].value);
        }
        given[f] = true;
    }
    LocalId base = {0};
    if (e->as.struct_.base) {
        base = to_local(l, lower_expr(l, e->as.struct_.base));
    }
    for (uint32_t f = 0; f < info->count; f++) {
        if (given[f]) {
            continue;
        }
        items[f] = e->as.struct_.base
                       ? base_field(l, base, f, struct_field_type(l->sema, e->type, f))
                       : lower_expr(l, info->defaults[f]);   // evaluated at each use
    }
    return aggregate(l, e->type, items, info->count);
}

Operand lower_constructor(Lowerer *l, Expr *call) {
    ArgList args = call->as.call.args;
    Operand *items = ARENA_ARRAY(&l->sema->session->arena, Operand, args.len + 1);
    for (uint32_t i = 0; i < args.len; i++) {
        items[i] = lower_expr(l, args.items[i].value);
        if (i + 1 < args.len) {
            items[i] = settle(l, items[i], args.items[i + 1].value);
        }
    }
    return aggregate(l, call->type, items, args.len);
}

void emit_store(Lowerer *l, Place place, Operand value) {
    AirStmt s = {.kind = AIR_ASSIGN, .dest = place.local, .dest_path = place.path,
                 .has_dest = true};
    s.as.rvalue = (Rvalue){.kind = RV_USE, .a = value};
    air_push(l->body, l->block, s);
}

Operand settle(Lowerer *l, Operand value, const Expr *next) {
    bool may_assign = next->kind == EXPR_BLOCK || next->kind == EXPR_IF ||
                      next->kind == EXPR_UNSAFE;
    if (!may_assign || value.kind != OPERAND_COPY) {
        return value;
    }
    return op_copy(emit_temp(l, value.type, (Rvalue){.kind = RV_USE, .a = value}), value.type);
}
