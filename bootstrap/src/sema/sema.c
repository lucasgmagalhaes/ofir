// Semantic analysis state: the prelude and the modules of the program.

#include "sema/sema.h"

#include <string.h>

typedef struct {
    const char *name;
    DefKind kind;
    uint32_t value;   // a TYPE_* constant or a Builtin
} PreludeEntry;

static const PreludeEntry PRELUDE[] = {
    {"int", DEF_PRIMITIVE, TID_INT},     {"uint", DEF_PRIMITIVE, TID_U64},
    {"i8", DEF_PRIMITIVE, TID_I8},       {"i16", DEF_PRIMITIVE, TID_I16},
    {"i32", DEF_PRIMITIVE, TID_I32},     {"i64", DEF_PRIMITIVE, TID_I64},
    {"u8", DEF_PRIMITIVE, TID_U8},       {"u16", DEF_PRIMITIVE, TID_U16},
    {"u32", DEF_PRIMITIVE, TID_U32},     {"u64", DEF_PRIMITIVE, TID_U64},
    {"float", DEF_PRIMITIVE, TID_FLOAT}, {"f32", DEF_PRIMITIVE, TID_F32},
    {"f64", DEF_PRIMITIVE, TID_F64},     {"bool", DEF_PRIMITIVE, TID_BOOL},
    {"str", DEF_PRIMITIVE, TID_STR},
    {"print", DEF_BUILTIN, BUILTIN_PRINT}, {"println", DEF_BUILTIN, BUILTIN_PRINTLN},
    {"panic", DEF_BUILTIN, BUILTIN_PANIC},
};

static DefId add_def(Sema *sema, Def def) {
    VEC_PUSH(sema->defs, def);
    return (DefId){(uint32_t)sema->defs.len - 1};
}

void sema_init(Sema *sema, Session *session, Program program) {
    *sema = (Sema){.session = session, .program = program, .module = program.items[0].module};
    types_init(&sema->types, &session->arena);
    VEC_PUSH(sema->defs, (Def){0});   // DefId 0 means "none"
    Symbol names[ARRAY_LEN(PRELUDE)];
    for (size_t i = 0; i < ARRAY_LEN(PRELUDE); i++) {
        names[i] = intern(&session->names, sv_from_cstr(PRELUDE[i].name));
    }
    intern(&session->names, SV("self"));   // before the scope tables are sized
    intern(&session->names, SV("_"));
    scope_init(&sema->scope, session->names.count, program.len);
    for (size_t i = 0; i < ARRAY_LEN(PRELUDE); i++) {
        Def def = {.kind = PRELUDE[i].kind, .name = names[i], .in_prelude = true, .is_pub = true};
        if (def.kind == DEF_PRIMITIVE) {
            def.type = type_id(PRELUDE[i].value);
        } else {
            def.builtin = (Builtin)PRELUDE[i].value;
        }
        DefId id = add_def(sema, def);
        for (uint32_t m = 0; m < program.len; m++) {   // visible in every module
            sema->scope.tables[m][names[i].id] = id.id;
        }
    }
}

uint32_t sema_enter(Sema *sema, uint32_t index) {
    uint32_t previous = sema->current;
    sema->current = index;
    sema->module = sema->program.items[index].module;
    scope_use_module(&sema->scope, index);
    return previous;
}

void sema_free(Sema *sema) {
    VEC_FREE(sema->tests);
    VEC_FREE(sema->imports);
    types_free(&sema->types);
    VEC_FREE(sema->defs);
    scope_free(&sema->scope);
}


const Attribute *find_attribute(const Sema *sema, AttributeList attributes, const char *name) {
    for (uint32_t i = 0; i < attributes.len; i++) {
        if (sv_eq(symbol_name(&sema->session->names, attributes.items[i].name),
                  sv_from_cstr(name))) {
            return &attributes.items[i];
        }
    }
    return NULL;
}

bool module_in_std(const Sema *sema, uint32_t module) {
    StrView name = sema->program.items[module].name;
    return name.len >= 3 && memcmp(name.ptr, "std", 3) == 0 &&
           (name.len == 3 || name.ptr[3] == '.');
}

bool skipped_test(const Sema *sema, const Item *item) {
    if (item->kind != ITEM_FN || item->attributes.len == 0 ||
        !find_attribute(sema, item->attributes, "test")) {
        return false;
    }
    bool tested = sema->current == 0 || !module_in_std(sema, sema->current);
    return !sema->testing || !tested;
}

Def *lang_def(Sema *sema, TypeId type) {
    if (type.id >= TID_BUILTIN_COUNT || sema->lang[type.id].id == 0) {
        return NULL;
    }
    return sema_def(sema, sema->lang[type.id]);
}

TypeId owner_type(Sema *sema, const Item *owner) {
    for (uint32_t t = 0; t < TID_BUILTIN_COUNT; t++) {
        if (sema->lang[t].id && sema_def(sema, sema->lang[t])->item == owner) {
            return type_id(t);
        }
    }
    DefId id = scope_module_in(&sema->scope, sema->current, owner->name);
    for (uint32_t m = 0; id.id == 0 && m < sema->program.len; m++) {
        id = scope_module_in(&sema->scope, m, owner->name);
    }
    SymbolList params = item_type_params(owner);
    TypeId small[8];
    TypeId *args = params.len <= 8 ? small : ARENA_ARRAY(sema->types.arena, TypeId, params.len);
    for (uint32_t i = 0; i < params.len; i++) {
        args[i] = type_param(&sema->types, i, params.items[i]);
    }
    return type_intern(&sema->types, (Ty){.kind = TY_ADT, .data = id.id, .arg_count = params.len,
                                          .args = args});
}

SymbolList item_type_params(const Item *item) {
    switch (item->kind) {
    case ITEM_STRUCT: case ITEM_TUPLE_STRUCT: return item->as.struct_.type_params;
    case ITEM_ENUM: return item->as.enum_.type_params;
    default: return (SymbolList){0};
    }
}

uint32_t fn_type_param_count(const Def *def) {
    uint32_t owner = def->item ? item_type_params(def->item).len : 0;
    return owner + def->fn->type_params.len;
}

TypeId struct_field_type(Sema *sema, TypeId adt, uint32_t i) {
    const Ty *ty = type_get(&sema->types, adt);
    if (ty->kind != TY_ADT) {   // a primitive's fields, from its `@lang` declaration
        return lang_def(sema, adt)->fields->types[i];
    }
    TypeId field = sema_def(sema, (DefId){ty->data})->fields->types[i];
    return type_subst(&sema->types, field, ty->args, ty->arg_count);
}

TypeId variant_field_type(Sema *sema, TypeId adt, uint32_t v, uint32_t i) {
    const Ty *ty = type_get(&sema->types, adt);
    TypeId field = sema_def(sema, (DefId){ty->data})->variants->variants[v].types[i];
    return type_subst(&sema->types, field, ty->args, ty->arg_count);
}

StrView sema_type_name(const void *context, const Ty *ty) {
    const Sema *sema = context;
    Symbol name = ty->kind == TY_PARAM ? (Symbol){ty->inner.id} : sema->defs.data[ty->data].name;
    return symbol_name(&sema->session->names, name);
}

void note_import_use(Sema *sema, Symbol name) {
    DefId id = scope_module(&sema->scope, name);
    const Def *def = id.id ? sema_def(sema, id) : NULL;
    if (def && def->module == sema->current && !def->in_prelude && def->kind != DEF_MODULE) {
        return;   // the module's own declaration, not an import
    }
    // The imports are recorded module by module, in order.
    for (size_t i = sema->imports.len; i > 0; i--) {
        ImportUse *use = &sema->imports.data[i - 1];
        if (use->module < sema->current) {
            break;
        }
        if (use->module == sema->current && symbol_eq(use->name, name)) {
            use->used = true;
        }
    }
}

void warn_unused_imports(Sema *sema) {
    for (size_t i = 0; i < sema->imports.len; i++) {
        const ImportUse *use = &sema->imports.data[i];
        // std modules other than the root are checked only in part (their
        // bodies wait for the lowering): they are checked whole as roots.
        bool partly_checked = use->module != 0 && module_in_std(sema, use->module);
        bool prelude_root = sv_eq(sema->program.items[use->module].name, SV("std.core"));
        if (!use->used && !prelude_root && !partly_checked) {
            diag_warning(&sema->session->diags, "W0002", use->span,
                         "unused import `" SV_FMT "`",
                         SV_ARG(symbol_name(&sema->session->names, use->name)));
        }
    }
}
