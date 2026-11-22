// Struct fields, literals, tuple struct constructors and field access
// (spec 3.4).

#include "sema/check_internal.h"
#include "sema/suggest.h"

static StrView name_of(Checker *c, Symbol name) {
    return symbol_name(&c->sema->session->names, name);
}

static bool args_are_copy(Checker *c, const Ty *ty) {
    for (uint32_t i = 0; i < ty->arg_count; i++) {
        if (!type_is_copy(c, ty->args[i])) {
            return false;
        }
    }
    return true;
}

// Spec 2.9: Option<T> and Result<T, E> are Copy when their arguments are;
// other structs and enums when declared `copy`. A type parameter without
// bounds may stand for any type, so it is not Copy.
bool type_is_copy(Checker *c, TypeId type) {
    const Ty *ty = type_get(c->types, type);
    switch (ty->kind) {
    case TY_ADT: {
        if (ty->data == c->sema->option.id || ty->data == c->sema->result.id) {
            return args_are_copy(c, ty);
        }
        const Item *item = sema_def(c->sema, (DefId){ty->data})->item;
        return item->kind == ITEM_ENUM ? item->as.enum_.is_copy : item->as.struct_.is_copy;
    }
    case TY_TUPLE: return args_are_copy(c, ty);
    case TY_ARRAY: return type_is_copy(c, ty->inner);
    case TY_STR: case TY_PARAM: case TY_SLICE: return false;
    default: return true;
    }
}

// The fields of an instance of a generic type hold its type arguments by
// value: `Wrap<Node>` inside `Node` contains `Node`.
static void check_instance_fields(Checker *c, TypeId type, Span span) {
    const Ty *ty = type_get(c->types, type);
    const Def *def = sema_def(c->sema, (DefId){ty->data});
    if (def->kind == DEF_STRUCT && def->fields) {
        for (uint32_t i = 0; i < def->fields->count; i++) {
            check_not_recursive(c, struct_field_type(c->sema, type, i), span);
        }
    }
    for (uint32_t v = 0; def->kind == DEF_ENUM && def->variants && v < def->variants->count;
         v++) {
        for (uint32_t i = 0; i < def->variants->variants[v].count; i++) {
            check_not_recursive(c, variant_field_type(c->sema, type, v, i), span);
        }
    }
}

// A field of a struct or enum type that is still being laid out contains
// itself. Tuples and the type arguments of instances are looked into.
void check_not_recursive(Checker *c, TypeId type, Span span) {
    const Ty *ty = type_get(c->types, type);
    for (uint32_t i = 0; ty->kind == TY_TUPLE && i < ty->arg_count; i++) {
        check_not_recursive(c, ty->args[i], span);
    }
    if (ty->kind != TY_ADT) {
        return;
    }
    DefId id = {ty->data};
    Def *inner = sema_def(c->sema, id);
    bool instance = ty->arg_count > 0;
    if (inner->checking) {
        check_error(c, "E0318", span,
                    "a type cannot contain itself by value; use Box<T> or a collection");
        return;
    }
    if (inner->kind == DEF_STRUCT) {
        struct_fields(c, id);
    } else if (inner->kind == DEF_ENUM) {
        enum_variants(c, id);
    }
    if (instance) {
        check_instance_fields(c, type, span);
    }
}

static void fill_fields(Checker *c, Def *def, StructInfo *info) {
    const Item *item = def->item;
    for (uint32_t i = 0; i < info->count; i++) {
        const FieldDecl *field = &item->as.struct_.fields.items[i];
        info->names[i] = field->name;
        info->types[i] = resolve_type(c, field->type);
        info->defaults[i] = field->default_value;
        if (!item->as.struct_.is_ref) {
            check_storable(c, info->types[i], field->span, "a struct field");
        }
        check_not_recursive(c, info->types[i], field->span);
        if (field->default_value) {
            coerce(c, field->default_value, info->types[i]);
        }
        if (info->is_copy && !type_is_copy(c, info->types[i]) && info->types[i].id != TID_ERROR) {
            check_error(c, "E0317", field->span,
                        "a `copy struct` can only have fields of Copy types, not `%s`",
                        type_name(c, info->types[i]));
        }
    }
}

StructInfo *struct_fields(Checker *c, DefId id) {
    Def *def = sema_def(c->sema, id);
    if (def->fields || def->checking) {
        return def->fields;
    }
    const Item *item = def->item;
    Arena *arena = &c->sema->session->arena;
    StructInfo *info = ARENA_NEW(arena, StructInfo);
    info->count = item->as.struct_.fields.len;
    info->names = ARENA_ARRAY(arena, Symbol, info->count + 1);
    info->types = ARENA_ARRAY(arena, TypeId, info->count + 1);
    info->defaults = ARENA_ARRAY(arena, Expr *, info->count + 1);
    info->is_tuple = item->kind == ITEM_TUPLE_STRUCT;
    info->is_copy = item->as.struct_.is_copy;
    uint32_t saved_module = sema_enter(c->sema, def->module);
    TypeId saved_self = c->self_type;
    Generics outer = enter_generics(c, (Generics){.owner = item->as.struct_.type_params,
                                                  .owner_bounds = item->bounds});
    c->self_type = def->lang ? def->type : owner_type(c->sema, item);
    def->checking = true;
    fill_fields(c, def, info);
    def->checking = false;
    enter_generics(c, outer);
    c->self_type = saved_self;
    sema_enter(c->sema, saved_module);
    def->fields = info;
    return info;
}

// The struct definition of a type (a `@lang` declaration for a primitive),
// or NULL.
static const Def *struct_def(Checker *c, TypeId type, DefId *id) {
    const Ty *ty = type_get(c->types, type);
    if (ty->kind != TY_ADT) {
        Def *lang = lang_def(c->sema, type);
        if (lang) {
            *id = c->sema->lang[type.id];
        }
        return lang;
    }
    *id = (DefId){ty->data};
    const Def *def = sema_def(c->sema, *id);
    return def->kind == DEF_STRUCT ? def : NULL;
}

static int64_t field_index(const StructInfo *info, Symbol name) {
    for (uint32_t i = 0; i < info->count; i++) {
        if (symbol_eq(info->names[i], name)) {
            return i;
        }
    }
    return -1;
}

TypeId check_struct_field(Checker *c, Expr *e, TypeId base) {
    DefId id;
    const Def *def = struct_def(c, base, &id);
    StructInfo *info = def ? struct_fields(c, id) : NULL;
    int64_t index = info && !info->is_tuple ? field_index(info, e->as.field.name) : -1;
    if (index < 0) {
        if (base.id != TID_ERROR && (!def || info)) {
            check_error(c, "E0316", e->span, "no field `" SV_FMT "` on type `%s`",
                        SV_ARG(name_of(c, e->as.field.name)), type_name(c, base));
            suggest_field(c->sema, info, e->as.field.name);
        }
        return type_id(TID_ERROR);
    }
    e->as.field.res = (Res){RES_FIELD, (uint32_t)index};
    check_visible(c, def->module, def->item->as.struct_.fields.items[index].is_pub,
                  e->as.field.name, e->span);
    return struct_field_type(c->sema, base, (uint32_t)index);
}

TypeId check_tuple_field(Checker *c, Expr *e) {
    TypeId base = check_expr(c, e->as.tuple_field.base, NO_TYPE);
    base = auto_deref(c, &e->as.tuple_field.base, base);
    const Ty *ty = type_get(c->types, base);
    if (ty->kind == TY_TUPLE && e->as.tuple_field.index < ty->arg_count) {
        return ty->args[e->as.tuple_field.index];
    }
    DefId id;
    const Def *def = struct_def(c, base, &id);
    StructInfo *info = def ? struct_fields(c, id) : NULL;
    uint32_t index = e->as.tuple_field.index;
    if (!info || !info->is_tuple || index >= info->count) {
        if (base.id != TID_ERROR) {
            check_error(c, "E0316", e->span, "no field `%u` on type `%s`", index,
                        type_name(c, base));
        }
        return type_id(TID_ERROR);
    }
    return struct_field_type(c->sema, base, index);
}

// `Name(a, b)` or `Name<T>(a, b)` for a tuple struct.
TypeId check_constructor(Checker *c, Expr *call, DefId id, TypeList explicit, TypeId expected) {
    StructInfo *info = struct_fields(c, id);
    const Item *item = sema_def(c->sema, id)->item;
    TypeId type = owner_type(c->sema, item);
    if (!info || !info->is_tuple) {
        check_error(c, "E0310", call->span, "expected a value, found type `" SV_FMT "`; "
                    "build a struct with `Name{field: value}`",
                    SV_ARG(name_of(c, sema_def(c->sema, id)->name)));
        return type_id(TID_ERROR);
    }
    Ty sig = {.kind = TY_FN, .inner = type, .arg_count = info->count, .args = info->types};
    Generics params = {.owner = item->as.struct_.type_params, .owner_bounds = item->bounds};
    if (params.owner.len == 0 && explicit.len == 0) {
        return check_args(c, call, &sig);
    }
    Infer inf = infer_new(c, params);
    if (explicit.len > 0) {
        infer_explicit(c, &inf, true, explicit, call->as.call.callee->span);
    }
    return check_generic_args(c, call, &sig, &inf, expected);
}

// Checks the field values; with `inf`, a generic struct's type parameters
// are bound from their types, which are checked by require_fields after.
static void check_literal_fields(Checker *c, Expr *e, const Def *def, bool *given, Infer *inf) {
    const StructInfo *info = def->fields;
    for (uint32_t i = 0; i < e->as.struct_.fields.len; i++) {
        FieldInit *init = &e->as.struct_.fields.items[i];
        int64_t index = field_index(info, init->name);
        if (index < 0) {
            check_error(c, "E0316", init->value->span, "no field `" SV_FMT "` on this struct",
                        SV_ARG(name_of(c, init->name)));
            suggest_field(c->sema, info, init->name);
            check_expr(c, init->value, NO_TYPE);
            continue;
        }
        check_visible(c, def->module, def->item->as.struct_.fields.items[index].is_pub,
                      init->name, init->value->span);
        if (given[index]) {
            check_error(c, "E0201", init->value->span, "field `" SV_FMT "` is given twice",
                        SV_ARG(name_of(c, init->name)));
        }
        given[index] = true;
        if (!inf) {
            coerce(c, init->value, info->types[index]);
            continue;
        }
        TypeId found = check_expr(c, init->value, infer_hint(c, inf, info->types[index]));
        infer_from_arg(c, inf, info->types[index], found);
    }
}

static void require_fields(Checker *c, Expr *e, const StructInfo *info, const Infer *inf) {
    for (uint32_t i = 0; i < e->as.struct_.fields.len; i++) {
        FieldInit *init = &e->as.struct_.fields.items[i];
        int64_t index = field_index(info, init->name);
        if (index >= 0) {
            require_type(c, init->value, init->value->type,
                         infer_apply(c, inf, info->types[index]));
        }
    }
}

// The type of a literal of a generic struct, `Pair{a: 1, b: "x"}` or
// `Pair<int, str>{...}`: its arguments come from the name, the expected
// type, then the field values.
static TypeId generic_literal(Checker *c, Expr *e, const Def *def, bool *given, TypeId expected) {
    Expr *name = e->as.struct_.type;
    Infer inf = infer_new(c, (Generics){.owner = def->item->as.struct_.type_params,
                                        .owner_bounds = def->item->bounds});
    if (name->kind == EXPR_GENERIC) {
        infer_explicit(c, &inf, true, name->as.generic.args, name->span);
    }
    infer_from(c, &inf, owner_type(c->sema, def->item), expected);
    check_literal_fields(c, e, def, given, &inf);
    infer_finish(c, &inf, name->span);
    require_fields(c, e, def->fields, &inf);
    return infer_apply(c, &inf, owner_type(c->sema, def->item));
}

// `Point{x: 1, y}`, `Config{}`, `User{age: 31, ..u}`, `Pair<int, str>{...}`.
TypeId check_struct_literal(Checker *c, Expr *e, TypeId expected) {
    Expr *name = e->as.struct_.type;
    Res res = name->kind == EXPR_IDENT ? name->as.ident.res
              : name->kind == EXPR_GENERIC ? name->as.generic.res : (Res){0};
    if (res.kind != RES_DEF) {
        return unsupported(c, name->span, "this struct literal is");
    }
    DefId id = {res.index};
    const Def *def = sema_def(c->sema, id);
    StructInfo *info = def->kind == DEF_STRUCT ? struct_fields(c, id) : NULL;
    if (!info || info->is_tuple) {
        check_error(c, "E0310", name->span, "`" SV_FMT "` is not a struct with named fields",
                    SV_ARG(name_of(c, def->name)));
        return type_id(TID_ERROR);
    }
    bool *given = ARENA_ARRAY(&c->sema->session->arena, bool, info->count + 1);
    bool generic = def->item->as.struct_.type_params.len > 0 || name->kind == EXPR_GENERIC;
    TypeId type = generic ? generic_literal(c, e, def, given, expected)
                          : type_intern(c->types, (Ty){.kind = TY_ADT, .data = id.id});
    name->type = type;
    if (!generic) {
        check_literal_fields(c, e, def, given, NULL);
    }
    if (e->as.struct_.base) {
        coerce(c, e->as.struct_.base, type);
        return type;
    }
    for (uint32_t i = 0; i < info->count; i++) {
        if (!given[i] && !info->defaults[i]) {
            check_error(c, "E0308", e->span, "missing field `" SV_FMT "` in `" SV_FMT "`",
                        SV_ARG(name_of(c, info->names[i])), SV_ARG(name_of(c, def->name)));
        }
    }
    return type;
}

// The fields of `@lang("str")` must be the layout the compiler and the
// runtime use: `{ptr: *u8, len: int, cap: int}`.
void check_lang_layout(Checker *c, const Def *lang, TypeId type) {
    const StructInfo *info = lang->fields;
    if (type.id == TID_STR) {
        TypeId bytes = type_intern(c->types, (Ty){.kind = TY_PTR, .inner = type_id(TID_U8)});
        bool ok = info && info->count == 3 && type_eq(info->types[0], bytes) &&
                  info->types[1].id == TID_INT && info->types[2].id == TID_INT;
        if (!ok) {
            // GCOVR_EXCL_START: only std declares `@lang` types, with these layouts
            check_error(c, "E0806", lang->span,
                        "`@lang(\"str\")` must declare `ptr: *u8`, `len: int`, `cap: int`");
            // GCOVR_EXCL_STOP
        }
    } else if (info && info->count > 0) {
        // GCOVR_EXCL_START: only std declares `@lang` types, with these layouts
        check_error(c, "E0806", lang->span, "a `@lang` numeric type declares no fields");
        // GCOVR_EXCL_STOP
    }
}
