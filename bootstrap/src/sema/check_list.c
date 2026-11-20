// `List<T>` (docs/std/collections.md): the parts the compiler provides on
// top of `std/core/list.of` — literals, indexing and `for` loops.

#include "sema/check_internal.h"

#include <string.h>

bool sequence_elem(Checker *c, TypeId type, TypeId *elem) {
    const Ty *ty = type_get(c->types, type);
    if (c->sema->slice.id != 0 && ty->kind == TY_ADT && ty->data == c->sema->slice.id) {
        *elem = ty->args[0];
        return true;
    }
    return list_elem(c, type, elem);
}

bool list_elem(Checker *c, TypeId type, TypeId *elem) {
    const Ty *ty = type_get(c->types, type);
    if (c->sema->list.id == 0 || ty->kind != TY_ADT || ty->data != c->sema->list.id ||
        ty->arg_count != 1) {
        return false;
    }
    *elem = ty->args[0];
    return true;
}

static TypeId list_of(Checker *c, TypeId elem) {
    return type_intern(c->types, (Ty){.kind = TY_ADT, .data = c->sema->list.id, .arg_count = 1,
                                      .args = &elem});
}

void require_copy(Checker *c, Span span, TypeId type, const char *what) {
    if (!type_is_copy(c, type) && type.id != TID_ERROR) {
        check_error(c, "E0321", span, "%s needs a value of a Copy type, not `%s`", what,
                    type_name(c, type));
    }
}

// `[a, b, c]` and `[]`: the elements have one type, from the expected
// `List<T>` or the first element.
TypeId check_list_literal(Checker *c, Expr *e, TypeId expected) {
    if (c->sema->list.id == 0) {
        return unsupported(c, e->span, "list literals without the standard library are");
    }
    TypeId elem = NO_TYPE;
    bool known = sequence_elem(c, expected, &elem);   // a List or a `[T]` parameter
    ExprList items = e->as.elems;
    for (uint32_t i = 0; i < items.len; i++) {
        if (known) {
            coerce(c, items.items[i], elem);
        } else {
            elem = check_expr(c, items.items[i], NO_TYPE);
            known = true;
        }
    }
    if (!known) {
        check_error(c, "E0330", e->span, "cannot infer the element type of `[]`: write the "
                    "type, as in `const names: List<str> = []`");
        return type_id(TID_ERROR);
    }
    TypeId type = list_of(c, elem);
    check_instance_storable(c, type, e->span);
    return type;
}

// `[value; count]`: `count` copies of a Copy value.
TypeId check_list_repeat(Checker *c, Expr *e, TypeId expected) {
    if (c->sema->list.id == 0) {
        return unsupported(c, e->span, "list literals without the standard library are");
    }
    TypeId elem;
    TypeId hint = list_elem(c, expected, &elem) ? elem : NO_TYPE;
    elem = check_expr(c, e->as.repeat.value, hint);
    require_copy(c, e->as.repeat.value->span, elem, "`[value; count]`");
    coerce(c, e->as.repeat.count, type_id(TID_INT));
    return list_of(c, elem);
}

bool map_parts(Checker *c, TypeId type, TypeId *key, TypeId *value) {
    const Ty *ty = type_get(c->types, type);
    if (c->sema->map.id == 0 || ty->kind != TY_ADT || ty->data != c->sema->map.id ||
        ty->arg_count != 2) {
        return false;
    }
    *key = ty->args[0];
    *value = ty->args[1];
    return true;
}

// `for x in list`, `for i, x in list` (with the index), `for key, value in
// map`. The variables borrow the elements (spec 5.5); stage 0 binds them to
// copies of the bits, which is the same while there are no drops (3.2).
void check_for_collection(Checker *c, Stmt *s) {
    Expr **expr = &s->as.for_.expr;
    TypeId type = auto_deref(c, expr, check_expr(c, *expr, NO_TYPE));
    bool two = s->as.for_.second.id != 0;
    TypeId first = type_id(TID_ERROR), second = type_id(TID_ERROR), elem, key, value;
    if (sequence_elem(c, type, &elem)) {
        first = two ? type_id(TID_INT) : elem;
        second = elem;
    } else if (map_parts(c, type, &key, &value)) {
        if (!two) {
            check_error(c, "E0300", (*expr)->span, "iterate a map with two variables: "
                        "`for key, value in map`");
        }
        first = key;
        second = value;
    } else if (iterator_item(c, type, &elem)) {
        first = check_for_iterator(c, s, elem);
    } else if (type.id != TID_ERROR) {
        check_error(c, "E0300", (*expr)->span, "mismatched types: `for` needs a range, a "
                    "list, a slice, a map or an iterator, found %s", type_name(c, type));
    }
    c->local_types[s->as.for_.first_local.id] = first;
    note_owned(c, first);
    if (two) {
        c->local_types[s->as.for_.second_local.id] = second;
        note_owned(c, second);
    }
}

// `xs[a..b]`, `xs[a..]`, `xs[..b]`: rewritten to `xs.slice(a, b)` or
// `xs.sliceFrom(a)`, methods of List, Slice and str.
TypeId check_range_index(Checker *c, Expr *e) {
    Arena *arena = &c->sema->session->arena;
    Expr *base = e->as.index.base, *range = e->as.index.index;
    Expr *lo = range->as.range.lo;
    if (!lo) {
        lo = ARENA_NEW(arena, Expr);
        *lo = (Expr){.kind = EXPR_INT, .span = range->span};
    }
    Expr *hi = range->as.range.hi;
    if (hi && range->as.range.inclusive) {   // `a..=b` is `a..b + 1`
        Expr *one = ARENA_NEW(arena, Expr);
        *one = (Expr){.kind = EXPR_INT, .span = hi->span, .as.int_value = 1};
        Expr *sum = ARENA_NEW(arena, Expr);
        *sum = (Expr){.kind = EXPR_BINARY, .span = hi->span};
        sum->as.binary.op = OP_ADD;
        sum->as.binary.lhs = hi;
        sum->as.binary.rhs = one;
        hi = sum;
    }
    uint32_t count = hi ? 2 : 1;
    Arg *args = ARENA_ARRAY(arena, Arg, 2);
    args[0] = (Arg){.value = lo};
    args[1] = (Arg){.value = hi};
    Expr *field = ARENA_NEW(arena, Expr);
    *field = (Expr){.kind = EXPR_FIELD, .span = e->span};
    field->as.field.base = base;
    field->as.field.name = intern(&c->sema->session->names, hi ? SV("slice") : SV("sliceFrom"));
    e->kind = EXPR_CALL;
    e->as.call.callee = field;
    e->as.call.args = (ArgList){.items = args, .len = count};
    e->as.call.type_args = (TypeId){0};
    return check_method_call(c, e, NO_TYPE);
}

// The fields the compiler reads: `ptr: *mut T`, `len: int`, then `cap`.
void check_list_layout(Checker *c, const Def *def) {
    const StructInfo *info = def->fields;
    const Ty *ptr = info && info->count == 3 ? type_get(c->types, info->types[0]) : NULL;
    if (!ptr || ptr->kind != TY_PTR || info->types[1].id != TID_INT) {
        // GCOVR_EXCL_START: only std declares `@lang` types, with these layouts
        check_error(c, "E0806", def->span,
                    "`@lang(\"list\")` must declare `ptr: *mut T`, `len: int`, `cap: int`");
        // GCOVR_EXCL_STOP
    }
}

static FnDecl *list_method(Checker *c, const char *name) {
    FnList methods = sema_def(c->sema, c->sema->list)->item->as.struct_.methods;
    for (uint32_t i = 0; i < methods.len; i++) {
        if (sv_eq(symbol_name(&c->sema->session->names, methods.items[i]->name),
                  sv_from_cstr(name))) {
            return methods.items[i];
        }
    }
    return NULL;   // GCOVR_EXCL_LINE: std's List declares asSlice
}

// A `List<T>` where a `&[T]` is expected (spec 2.12) becomes `list.asSlice()`.
bool coerce_to_slice(Checker *c, Expr *e, TypeId found, TypeId expected) {
    TypeId elem, wanted;
    const Ty *ty = type_get(c->types, expected);
    if (ty->kind != TY_ADT || ty->data != c->sema->slice.id || c->sema->slice.id == 0 ||
        !list_elem(c, found, &elem) || !type_eq(elem, ty->args[0])) {
        return false;
    }
    wanted = expected;
    FnDecl *as_slice = list_method(c, "asSlice");
    if (!as_slice) {
        return false;   // GCOVR_EXCL_LINE: std's List declares asSlice
    }
    fn_signature(c, as_slice);
    Arena *arena = &c->sema->session->arena;
    Expr *list = ARENA_NEW(arena, Expr);
    *list = *e;
    Expr *field = ARENA_NEW(arena, Expr);
    *field = (Expr){.kind = EXPR_FIELD, .span = e->span};
    field->as.field.base = list;
    field->as.field.name = as_slice->name;
    field->as.field.res = (Res){RES_DEF, as_slice->def.id};
    *e = (Expr){.kind = EXPR_CALL, .span = e->span, .type = wanted};
    e->as.call.callee = field;
    e->as.call.type_args = type_tuple(c->types, &elem, 1);
    return true;
}
