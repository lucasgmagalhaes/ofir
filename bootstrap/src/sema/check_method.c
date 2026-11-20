// Type checking of method calls, fields and indexing. Stage 0 knows only
// the built-in methods of the primitive types (sema/builtins.c).

#include "sema/builtins.h"
#include "sema/check_internal.h"
#include "sema/suggest.h"

static StrView name_of(Checker *c, Symbol name) {
    return symbol_name(&c->sema->session->names, name);
}

// The primitive type named by `e` (as in `str.new()` or `i32.MAX`), or NULL.
static const Def *primitive_name(Checker *c, const Expr *e) {
    if (e->kind != EXPR_IDENT || e->as.ident.res.kind != RES_DEF) {
        return NULL;
    }
    const Def *def = sema_def(c->sema, (DefId){e->as.ident.res.index});
    return def->kind == DEF_PRIMITIVE ? def : NULL;
}

Expr *place_root(Expr *e) {
    for (;;) {
        if (e->kind == EXPR_FIELD) {
            e = e->as.field.base;
        } else if (e->kind == EXPR_TUPLE_FIELD) {
            e = e->as.tuple_field.base;
        } else if (e->kind == EXPR_INDEX && e->as.index.base->type.id != TID_STR) {
            e = e->as.index.base;   // an element of a list
        } else {
            bool deref = e->kind == EXPR_UNARY && e->as.unary.op == OP_DEREF;
            return e->kind == EXPR_IDENT || e->kind == EXPR_SELF || deref ? e : NULL;
        }
    }
}

// Whether the place goes through an element of a `&[T]`, which is read-only.
static bool through_slice(Checker *c, const Expr *e) {
    for (;;) {
        if (e->kind == EXPR_FIELD) {
            e = e->as.field.base;
        } else if (e->kind == EXPR_TUPLE_FIELD) {
            e = e->as.tuple_field.base;
        } else if (e->kind == EXPR_INDEX) {
            const Ty *ty = type_get(c->types, e->as.index.base->type);
            if (ty->kind == TY_ADT && ty->data == c->sema->slice.id && ty->data != 0) {
                return true;
            }
            e = e->as.index.base;
        } else {
            return false;
        }
    }
}

void require_mutable(Checker *c, Expr *place) {
    if (through_slice(c, place)) {
        check_error(c, "E0315", place->span, "this value cannot be modified: the elements of "
                    "a `&[T]` are read-only");
        return;
    }
    Expr *e = place_root(place);
    if (!e) {
        if (place->type.id != TID_ERROR) {
            check_error(c, "E0315", place->span, "only variables and their fields can be modified");
        }
        return;
    }
    bool ok = false;
    if (e->kind == EXPR_UNARY && is_box_deref(c, e)) {   // through a box: the box is modified
        require_mutable(c, e->as.unary.operand->as.field.base);
        return;
    }
    if (e->kind == EXPR_UNARY) {   // `*p`: writable through a `*mut T` or a `&mut T`
        const Ty *ptr = type_get(c->types, e->as.unary.operand->type);
        ok = ((ptr->kind == TY_PTR || ptr->kind == TY_REF) && ptr->data) ||
             e->type.id == TID_ERROR;
        if (!ok) {
            check_error(c, "E0315", e->span, "this value cannot be modified: it is reached "
                        "through a `%s`, not a `%s`", ptr->kind == TY_REF ? "&T" : "*T",
                        ptr->kind == TY_REF ? "&mut T" : "*mut T");
        }
        return;
    }
    if ((e->kind == EXPR_IDENT || e->kind == EXPR_SELF) && e->as.ident.res.kind == RES_LOCAL) {
        ok = c->fn->locals.items[e->as.ident.res.index].mutable;
    } else if (e->kind == EXPR_IDENT && e->as.ident.res.kind == RES_DEF) {
        ok = sema_def(c->sema, (DefId){e->as.ident.res.index})->kind == DEF_GLOBAL;
    } else {
        // GCOVR_EXCL_START: a name that resolves to nothing stops before checking
        ok = e->type.id == TID_ERROR;
        // GCOVR_EXCL_STOP
    }
    if (!ok) {
        check_error(c, "E0315", e->span, "this value cannot be modified: declare it with `let`");
    }
}

static TypeId param_type(Checker *c, BuiltinType t, TypeId self, TypeId target) {
    switch (t) {
    case BT_OPTION:
        if (c->sema->option.id == 0 || target.id == TID_ERROR) {
            return type_id(TID_ERROR);
        }
        return type_intern(c->types, (Ty){.kind = TY_ADT, .data = c->sema->option.id,
                                          .arg_count = 1, .args = &target});
    case BT_SELF: return self;
    case BT_INT: return type_id(TID_INT);
    case BT_TARGET: return target;
    // GCOVR_EXCL_START: a BT_NONE parameter or result is never looked up
    case BT_NONE: return type_id(TID_UNIT);
    // GCOVR_EXCL_STOP
    }
    return type_id(TID_ERROR);   // GCOVR_EXCL_LINE: the switch covers every builtin type
}

// The integer type argument of `x.wrappingCast<T>()`.
static TypeId type_argument(Checker *c, Expr *field, const BuiltinInfo *info) {
    TypeList args = field->as.field.type_args;
    if (args.len != (info->type_arg ? 1u : 0u)) {
        check_error(c, "E0302", field->span,
                    "wrong number of type arguments: expected %u, found %u",
                    info->type_arg ? 1u : 0u, args.len);
        return type_id(TID_ERROR);
    }
    if (!info->type_arg) {
        return type_id(TID_ERROR);
    }
    TypeId target = resolve_type(c, args.items[0]);
    if (!type_is_integer(c->types, target) && target.id != TID_ERROR) {
        check_error(c, "E0300", args.items[0]->span,
                    "mismatched types: expected an integer type, found %s", type_name(c, target));
        return type_id(TID_ERROR);
    }
    return target;
}

static TypeId call_builtin(Checker *c, Expr *call, const BuiltinInfo *info, TypeId self) {
    Expr *field = call->as.call.callee;
    field->as.field.res = (Res){RES_BUILTIN, info->method};
    if (info->self_mode == MODE_MUT) {
        // GCOVR_EXCL_START: no built-in method takes `mut self`
        require_mutable(c, field->as.field.base);
        // GCOVR_EXCL_STOP
    }
    if (info->receiver == RECV_PTR) {   // pointer arithmetic (spec 11.2)
        require_unsafe(c, call->span, "offsetting a raw pointer");
    }
    TypeId target = type_argument(c, field, info);
    TypeId params[2];
    uint32_t count = 0;
    while (count < 2 && info->params[count] != BT_NONE) {
        params[count] = param_type(c, info->params[count], self, target);
        count++;
    }
    TypeId ret = info->ret == BT_NONE ? type_id(TID_UNIT) : param_type(c, info->ret, self, target);
    Ty sig = {.kind = TY_FN, .inner = ret, .arg_count = count, .args = params};
    return check_args(c, call, &sig);
}

static Receiver receiver_of(const Ty *ty, bool *ok) {
    *ok = ty->kind == TY_INT || ty->kind == TY_PTR;
    return ty->kind == TY_INT ? RECV_INT : RECV_PTR;
}

// Whether std declares the method `name` for the primitive `type`; the
// built-in table answers the others until they all move to std.
static bool has_lang_method(Checker *c, TypeId type, Symbol name) {
    const Def *lang = lang_def(c->sema, type);
    if (!lang) {
        return false;
    }
    FnList methods = lang->item->as.struct_.methods;
    for (uint32_t i = 0; i < methods.len; i++) {
        if (symbol_eq(methods.items[i]->name, name)) {
            return true;
        }
    }
    return false;
}

static TypeId lang_associated(Checker *c, Expr *call, TypeId type, TypeId expected) {
    call->as.call.callee->as.field.base->type = type;
    return check_user_method(c, call, c->sema->lang[type.id], true, (TypeHints){type, expected});
}

// `Point.new()`, `Stack<int>.new()` or `Shape.Circle(1.0)`.
static TypeId call_on_type(Checker *c, Expr *call, DefId owner, TypeList args, TypeId expected) {
    Expr *field = call->as.call.callee;
    TypeHints hints = {named_instance(c, owner, args, field->as.field.base->span), expected};
    field->as.field.base->type = hints.self;
    TypeId variant;
    if (sema_def(c->sema, owner)->kind == DEF_ENUM &&
        check_qualified_variant(c, call, owner, hints, &variant)) {
        return variant;
    }
    return check_user_method(c, call, owner, true, hints);
}

static TypeId no_method(Checker *c, Expr *call, TypeId self) {
    Expr *field = call->as.call.callee;
    if (self.id != TID_ERROR) {
        check_error(c, "E0301", field->span, "no method `" SV_FMT "` on type `%s`",
                    SV_ARG(name_of(c, field->as.field.name)), type_name(c, self));
        const Def *lang = lang_def(c->sema, self);
        if (lang && lang->item) {
            suggest_method(c->sema, lang->item, field->as.field.name);
        }
    }
    for (uint32_t i = 0; i < call->as.call.args.len; i++) {
        check_expr(c, call->as.call.args.items[i].value, NO_TYPE);
    }
    return type_id(TID_ERROR);
}

// `value.m(args)` on a primitive: a `@lang` method from std, else one of
// the few built-in ones.
static TypeId primitive_method(Checker *c, Expr *call, TypeId self, TypeId expected) {
    Symbol name = call->as.call.callee->as.field.name;
    if (has_lang_method(c, self, name)) {
        return check_user_method(c, call, c->sema->lang[self.id], false,
                                 (TypeHints){self, expected});
    }
    bool known;
    Receiver receiver = receiver_of(type_get(c->types, self), &known);
    const BuiltinInfo *info = known ? builtin_method(&c->sema->session->names, receiver, name)
                                    : NULL;
    if (info) {
        return call_builtin(c, call, info, self);
    }
    if (sv_eq(name_of(c, name), SV("clone"))) {
        return check_derived_clone(c, call, self);
    }
    return no_method(c, call, self);
}

TypeId check_method_call(Checker *c, Expr *call, TypeId expected) {
    Expr *field = call->as.call.callee;
    Expr *base = field->as.field.base;
    DefId owner;
    TypeList args;
    if (type_name_of(c, base, &owner, &args)) {
        return call_on_type(c, call, owner, args, expected);
    }
    const Def *type_name_def = primitive_name(c, base);
    if (type_name_def) {   // an associated function: `str.new()`
        base->type = type_name_def->type;
        if (has_lang_method(c, type_name_def->type, field->as.field.name)) {
            return lang_associated(c, call, type_name_def->type, expected);
        }
        return no_method(c, call, type_name_def->type);
    }
    TypeId self = check_expr(c, base, NO_TYPE);
    self = method_receiver(c, &field->as.field.base, self, field->as.field.name);
    if (type_get(c->types, self)->kind == TY_ADT) {
        DefId id = {type_get(c->types, self)->data};
        return check_user_method(c, call, id, false, (TypeHints){self, expected});
    }
    return primitive_method(c, call, self, expected);
}

// `p.x` on a struct, or `i32.MAX`, `float.NAN`, ... on a primitive type.
TypeId check_field(Checker *c, Expr *e, TypeId expected) {
    DefId owner;
    TypeList args;
    Expr *base = e->as.field.base;
    if (type_name_of(c, base, &owner, &args)) {   // `Shape.Empty`, `Opt<int>.None`
        TypeId variant;
        base->type = named_instance(c, owner, args, base->span);
        if (sema_def(c->sema, owner)->kind == DEF_ENUM &&
            check_qualified_variant(c, e, owner, (TypeHints){base->type, expected}, &variant)) {
            return variant;
        }
        check_error(c, "E0320", e->span, "no variant `" SV_FMT "` in `" SV_FMT "`",
                    SV_ARG(name_of(c, e->as.field.name)),
                    SV_ARG(name_of(c, sema_def(c->sema, owner)->name)));
        return type_id(TID_ERROR);
    }
    const Def *primitive = primitive_name(c, e->as.field.base);
    if (!primitive) {
        TypeId type = check_expr(c, e->as.field.base, NO_TYPE);
        return check_struct_field(c, e, auto_deref(c, &e->as.field.base, type));
    }
    e->as.field.base->type = primitive->type;
    const Ty *ty = type_get(c->types, primitive->type);
    if (builtin_const(&c->sema->session->names, ty, e->as.field.name) == BC_NONE) {
        check_error(c, "E0301", e->span, "no constant `" SV_FMT "` on type `%s`",
                    SV_ARG(name_of(c, e->as.field.name)), type_name(c, primitive->type));
        return type_id(TID_ERROR);
    }
    return primitive->type;
}

// `s[i]` is the byte at offset i of a str (spec 2.3); `l[i]` an element.
TypeId check_index(Checker *c, Expr *e) {
    if (e->as.index.index->kind == EXPR_RANGE) {
        return check_range_index(c, e);
    }
    TypeId base = check_expr(c, e->as.index.base, NO_TYPE);
    base = auto_deref(c, &e->as.index.base, base);
    coerce(c, e->as.index.index, type_id(TID_INT));
    TypeId elem;
    if (sequence_elem(c, base, &elem)) {
        return elem;
    }
    if (base.id == TID_STR) {
        return type_id(TID_U8);
    }
    if (base.id != TID_ERROR) {
        check_error(c, "E0313", e->span, "type `%s` cannot be indexed", type_name(c, base));
    }
    return type_id(TID_ERROR);
}
