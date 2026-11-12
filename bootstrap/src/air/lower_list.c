// Lowering of `List<T>` literals, indexing and `for` loops. A literal calls
// the functions of `std/core/list.of`; an element is reached through the
// list's `ptr` and `len` fields (positions 0 and 1, checked by
// check_list_layout).

#include "air/lower_internal.h"

enum { LIST_PTR = 0, LIST_LEN = 1 };

// A list or a slice: both have `ptr` and `len` first.
bool is_list(Lowerer *l, TypeId type) {
    const Ty *ty = type_get(l->types, type);
    return ty->kind == TY_ADT && ty->data != 0 &&
           (ty->data == l->sema->list.id || ty->data == l->sema->slice.id);
}

static TypeId elem_of(Lowerer *l, TypeId list) {
    return type_get(l->types, list)->args[0];
}

static Place field_of(Lowerer *l, Place place, uint32_t field) {
    uint32_t *fields = ARENA_ARRAY(&l->sema->session->arena, uint32_t, place.path.len + 1);
    for (uint32_t i = 0; i < place.path.len; i++) {
        fields[i] = place.path.fields[i];
    }
    fields[place.path.len] = field;
    place.path.fields = fields;
    place.path.len++;
    return place;
}

// Calls `List<elem>.name(args)`; `dest` receives the result unless NULL.
static void call_list(Lowerer *l, const char *name, TypeId elem, CallArgs args, LocalId *dest) {
    FnList methods = sema_def(l->sema, l->sema->list)->item->as.struct_.methods;
    StrView wanted = sv_from_cstr(name);
    for (uint32_t i = 0; i < methods.len; i++) {
        if (sv_eq(symbol_name(&l->sema->session->names, methods.items[i]->name), wanted)) {
            emit_direct_call(l, methods.items[i], type_tuple(l->types, &elem, 1), args, dest);
            return;
        }
    }
    fatal("lowering: std/core/list.of has no `%s`", name);
}

// `[a, b, c]`: `withCapacity(3)`, then `push` of each element in order;
// `[]` is `new()`.
Operand lower_list_literal(Lowerer *l, Expr *e) {
    TypeId elem = elem_of(l, e->type);
    ExprList items = e->as.elems;
    LocalId list = new_temp(l, e->type);
    Arena *arena = &l->sema->session->arena;
    if (items.len == 0) {
        call_list(l, "new", elem, (CallArgs){0}, &list);
        return op_copy(list, e->type);
    }
    Operand *capacity = ARENA_NEW(arena, Operand);
    *capacity = op_int(items.len, type_id(TID_INT));
    call_list(l, "withCapacity", elem, (CallArgs){capacity, 1}, &list);
    Operand self = place_operand((Place){.local = list}, OPERAND_ADDR, e->type);
    for (uint32_t i = 0; i < items.len; i++) {
        Operand *args = ARENA_ARRAY(arena, Operand, 2);
        args[0] = self;
        args[1] = lower_expr(l, items.items[i]);
        call_list(l, "push", elem, (CallArgs){args, 2}, NULL);
    }
    return op_copy(list, e->type);
}

// `[value; count]`: `filled(value, count)`.
Operand lower_list_repeat(Lowerer *l, Expr *e) {
    Operand *args = ARENA_ARRAY(&l->sema->session->arena, Operand, 2);
    args[0] = lower_expr(l, e->as.repeat.value);
    args[0] = settle(l, args[0], e->as.repeat.count);
    args[1] = lower_expr(l, e->as.repeat.count);
    LocalId list = new_temp(l, e->type);
    call_list(l, "filled", elem_of(l, e->type), (CallArgs){args, 2}, &list);
    return op_copy(list, e->type);
}

// The address of element `index` of the list at `list`, after the check.
static LocalId element(Lowerer *l, Place list, TypeId type, Operand index, Span span) {
    Operand len = place_operand(field_of(l, list, LIST_LEN), OPERAND_COPY, type_id(TID_INT));
    emit_bounds_check(l, index, len, span);
    TypeId ptr = struct_field_type(l->sema, type, LIST_PTR);
    Operand base = place_operand(field_of(l, list, LIST_PTR), OPERAND_COPY, ptr);
    return emit_temp(l, ptr, (Rvalue){.kind = RV_PTR_ADD, .a = base, .b = index});
}

Place lower_list_index(Lowerer *l, Expr *e) {
    Place list = lower_place(l, e->as.index.base);
    Operand index = lower_expr(l, e->as.index.index);
    index = op_copy(to_local(l, index), type_id(TID_INT));
    LocalId at = element(l, list, e->as.index.base->type, index, e->span);
    return (Place){.local = at, .path = {.deref = true}};
}

static bool is_map(Lowerer *l, TypeId type) {
    const Ty *ty = type_get(l->types, type);
    return ty->kind == TY_ADT && ty->data != 0 && ty->data == l->sema->map.id;
}

// The fields of a map entry (std/core/map.of): key, value, hash.
enum { ENTRY_KEY = 0, ENTRY_VALUE = 1 };

// The current element of a loop: its address, type and position.
typedef struct {
    LocalId at;
    TypeId elem;
    Operand index;
} Element;

// Assigns a loop variable the value at `place` or, in a `for mut` loop, its
// address: the variable is then the element itself.
static void bind_value(Lowerer *l, Stmt *s, LocalId local, Place place, TypeId type) {
    Operand value = place_operand(place, OPERAND_COPY, type);
    if (s->as.for_.mutable) {
        l->body->locals.data[local.id].by_ref = true;
        value = place_operand(place, OPERAND_ADDR, type_ref(l->types, type, false));
    }
    emit_assign(l, local, (Rvalue){.kind = RV_USE, .a = value});
}

// Binds the loop variables to the current element: `x`, `i, x` or, for a
// map entry, `key, value`.
static void bind_element(Lowerer *l, Stmt *s, Element current) {
    Place element = {.local = current.at, .path = {.deref = true}};
    TypeId elem = current.elem;
    Operand index = current.index;
    LocalId first = air_local_of(s->as.for_.first_local);
    LocalId second = air_local_of(s->as.for_.second_local);
    if (!is_map(l, s->as.for_.expr->type)) {
        bool two = s->as.for_.second.id != 0;
        bind_value(l, s, two ? second : first, element, elem);
        if (two) {
            emit_assign(l, first, (Rvalue){.kind = RV_USE, .a = index});
        }
        return;
    }
    TypeId map = s->as.for_.expr->type;
    Operand key = place_operand(field_of(l, element, ENTRY_KEY), OPERAND_COPY,
                                type_get(l->types, map)->args[0]);
    emit_assign(l, first, (Rvalue){.kind = RV_USE, .a = key});
    bind_value(l, s, second, field_of(l, element, ENTRY_VALUE), type_get(l->types, map)->args[1]);
}

// `for x in list`, `for i, x in list` and `for key, value in map`: a hidden
// counter over the list (a map's list of entries); its length and buffer
// are read at each iteration.
void lower_for_list(Lowerer *l, Stmt *s) {
    Expr *expr = s->as.for_.expr;
    Place list = lower_place(l, expr);
    TypeId type = expr->type;
    if (is_map(l, type)) {   // field 0 of a Map is its list of entries
        list = field_of(l, list, 0);
        type = struct_field_type(l->sema, type, 0);
    }
    TypeId int_type = type_id(TID_INT);
    LocalId counter = emit_temp(l, int_type, (Rvalue){.kind = RV_USE, .a = op_int(0, int_type)});
    Operand current = op_copy(counter, int_type);
    BlockId header = air_new_block(l->body), body = air_new_block(l->body);
    BlockId latch = air_new_block(l->body), exit = air_new_block(l->body);
    emit_goto(l, header);
    switch_to(l, header);
    Operand len = place_operand(field_of(l, list, LIST_LEN), OPERAND_COPY, int_type);
    Rvalue test = {.kind = RV_BINARY, .op = OP_LT, .a = current, .b = len};
    Operand go = op_copy(emit_temp(l, type_id(TID_BOOL), test), type_id(TID_BOOL));
    terminate(l, (Terminator){.kind = TERM_BRANCH, .cond = go, .target = body, .other = exit});
    switch_to(l, body);
    TypeId ptr = struct_field_type(l->sema, type, LIST_PTR);
    Operand base = place_operand(field_of(l, list, LIST_PTR), OPERAND_COPY, ptr);
    LocalId at = emit_temp(l, ptr, (Rvalue){.kind = RV_PTR_ADD, .a = base, .b = current});
    bind_element(l, s, (Element){at, elem_of(l, type), current});
    lower_loop_body(l, s->as.for_.body, latch, exit);
    switch_to(l, latch);   // counter < len: the increment cannot overflow
    emit_assign(l, counter, (Rvalue){.kind = RV_BINARY, .op = OP_ADD, .a = current,
                                     .b = op_int(1, int_type)});
    emit_goto(l, header);
    switch_to(l, exit);
}
