// Name resolution, first pass: the prelude and the top-level declarations
// (docs/design/compiler-architecture.md, 5). Bodies are in resolve_body.c.

#include "sema/resolve_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static DefId add_def(Sema *sema, Def def) {
    VEC_PUSH(sema->defs, def);
    return (DefId){(uint32_t)sema->defs.len - 1};
}

void resolve_error(Resolver *r, const char *code, Span span, const char *format, ...) {
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    diag_error(&r->sema->session->diags, code, span, "%s", message);
}

StrView resolver_name(const Resolver *r, Symbol name) {
    return symbol_name(&r->sema->session->names, name);
}

// ---------------------------------------------------------------------------
// --emit=names: a trace of what the root module's names resolve to, which
// stage 1's resolver must reproduce (roadmap 4.5).

bool tracing(const Resolver *r) {
    return r->sema->trace && r->sema->current == 0;
}

void trace(Resolver *r, Span span, const char *format, ...) {
    if (!tracing(r)) {
        return;
    }
    LineCol at = source_line_col(&r->sema->session->sources, span.file, span.start);
    fprintf(r->sema->trace, "%u:%u ", at.line, at.column);
    va_list args;
    va_start(args, format);
    vfprintf(r->sema->trace, format, args);
    va_end(args);
    fputc('\n', r->sema->trace);
}

static const char *const DEF_KINDS[] = {
    [DEF_FN] = "fn", [DEF_BUILTIN] = "builtin", [DEF_PRIMITIVE] = "primitive",
    [DEF_STRUCT] = "struct", [DEF_ENUM] = "enum", [DEF_CONST] = "const",
    [DEF_GLOBAL] = "global", [DEF_TYPE_ALIAS] = "alias", [DEF_MODULE] = "module",
    [DEF_VARIANT] = "variant",
};

// `local 3`, `fn std.core.str.len`, `module std.fs`, `error`.
static void res_text(Resolver *r, Res res, StrBuf *out) {
    if (res.kind == RES_LOCAL) {
        strbuf_appendf(out, "local %u", res.index);
        return;
    }
    if (res.kind != RES_DEF || res.index == 0) {
        strbuf_append_cstr(out, "error");
        return;
    }
    const Def *def = sema_def(r->sema, (DefId){res.index});
    strbuf_append_cstr(out, DEF_KINDS[def->kind]);
    strbuf_append_cstr(out, " ");
    if (def->kind == DEF_MODULE) {
        strbuf_append(out, r->sema->program.items[def->target].name);
        return;
    }
    if (!def->in_prelude) {
        strbuf_append(out, r->sema->program.items[def->module].name);
        strbuf_append_cstr(out, ".");
    }
    strbuf_append(out, resolver_name(r, def->name));
}

void trace_use(Resolver *r, const char *what, Symbol name, Span span, Res res) {
    if (!tracing(r)) {
        return;
    }
    StrBuf text = {0};
    res_text(r, res, &text);
    trace(r, span, "%s " SV_FMT " -> %s", what, SV_ARG(resolver_name(r, name)), text.data);
    strbuf_free(&text);
}

// Registers a top-level name; a module declaration may hide a prelude name.
void declare_item(Resolver *r, Def def) {
    def.module = r->sema->current;
    DefId existing = scope_module(&r->sema->scope, def.name);
    if (existing.id != 0 && !sema_def(r->sema, existing)->in_prelude) {
        resolve_error(r, "E0201", def.span, "`" SV_FMT "` is defined more than once",
                      SV_ARG(resolver_name(r, def.name)));
        return;
    }
    scope_set_module(&r->sema->scope, def.name, add_def(r->sema, def));
}

static DefKind item_def_kind(ItemKind kind) {
    switch (kind) {
    case ITEM_STRUCT: case ITEM_TUPLE_STRUCT: return DEF_STRUCT;
    case ITEM_ENUM: return DEF_ENUM;
    case ITEM_CONST: return DEF_CONST;
    case ITEM_GLOBAL: return DEF_GLOBAL;
    case ITEM_TYPE_ALIAS: return DEF_TYPE_ALIAS;
    // GCOVR_EXCL_START: functions, imports and extern blocks never get here
    default: return DEF_FN;
    // GCOVR_EXCL_STOP
    }
}

static void declare_fn(Resolver *r, const Item *item, FnDecl *fn, bool in_module) {
    Def def = {.kind = DEF_FN, .name = fn->name, .span = fn->span, .item = item, .fn = fn,
               .is_pub = fn->is_pub, .module = r->sema->current};
    if (in_module) {
        declare_item(r, def);
        fn->def = scope_module(&r->sema->scope, fn->name);
    } else {
        fn->def = add_def(r->sema, def);   // methods are found through their type
    }
}

// Methods and fields share one namespace per type.
static void check_members(Resolver *r, const Item *item) {
    FnList methods = item->kind == ITEM_ENUM ? item->as.enum_.methods : item->as.struct_.methods;
    for (uint32_t i = 0; i < methods.len; i++) {
        FnDecl *fn = methods.items[i];
        for (uint32_t j = 0; j < i; j++) {
            if (symbol_eq(methods.items[j]->name, fn->name)) {
                resolve_error(r, "E0201", fn->span, "`" SV_FMT "` is defined more than once",
                              SV_ARG(resolver_name(r, fn->name)));
            }
        }
        declare_fn(r, item, fn, false);
    }
}

static bool in_std(Resolver *r) {
    StrView module = r->sema->program.items[r->sema->current].name;
    return module.len >= 3 && memcmp(module.ptr, "std", 3) == 0;
}

// `@lang("box") struct Box<T>` and `@lang("list") struct List<T>` in std:
// ordinary declarations that the compiler knows (it dereferences boxes and
// builds, indexes and iterates lists). Their slot in the Sema, or NULL.
static DefId *named_lang(Resolver *r, const Attribute *attr) {
    const Expr *arg = attr && attr->args.len == 1 ? attr->args.items[0] : NULL;
    if (!arg || arg->kind != EXPR_STR || !in_std(r)) {
        return NULL;
    }
    StrView name = arg->as.str;
    DefId *slot = sv_eq(name, SV("box"))      ? &r->sema->box
                  : sv_eq(name, SV("list"))   ? &r->sema->list
                  : sv_eq(name, SV("option")) ? &r->sema->option
                  : sv_eq(name, SV("result")) ? &r->sema->result
                  : sv_eq(name, SV("slice"))  ? &r->sema->slice
                  : sv_eq(name, SV("slice_mut")) ? &r->sema->slice_mut
                  : sv_eq(name, SV("map"))    ? &r->sema->map
                                              : NULL;
    return slot && slot->id == 0 ? slot : NULL;
}

// `@lang("str") struct str { ... }` in std: the body of a primitive type.
// It declares no name; the primitive gets its fields and methods.
static bool collect_lang(Resolver *r, const Item *item) {
    const Attribute *attr = find_attribute(r->sema, item->attributes, "lang");
    if (!attr || named_lang(r, attr)) {
        return false;
    }
    bool std_module = in_std(r);
    const Expr *arg = attr->args.len == 1 ? attr->args.items[0] : NULL;
    DefId primitive = arg && arg->kind == EXPR_STR
                          ? scope_module(&r->sema->scope, intern(&r->sema->session->names,
                                                                 arg->as.str))
                          : (DefId){0};
    const Def *prim = primitive.id ? sema_def(r->sema, primitive) : NULL;
    bool taken = prim && prim->kind == DEF_PRIMITIVE && r->sema->lang[prim->type.id].id;
    if (!std_module || !prim || prim->kind != DEF_PRIMITIVE || taken) {
        resolve_error(r, "E0805", attr->span,
                      std_module ? "`@lang` needs a primitive type or a std type name, declared "
                                   "once: `@lang(\"str\")`"
                                 : "`@lang` is reserved to the standard library");
        return true;
    }
    TypeId type = prim->type;   // `prim` points into `defs`, which the push may move
    VEC_PUSH(r->sema->defs, ((Def){.kind = DEF_STRUCT, .name = item->name, .span = item->span,
                                   .item = item, .is_pub = true, .lang = true,
                                   .type = type, .module = r->sema->current}));
    r->sema->lang[type.id] = (DefId){(uint32_t)r->sema->defs.len - 1};
    check_members(r, item);
    return true;
}

// The variants of Option and Result are names of their own: `Some(x)`,
// `None`, `Ok(v)`, `Err(e)`, which the prelude exports (spec 6.1).
static void declare_variants(Resolver *r, const Item *item, DefId owner) {
    for (uint32_t i = 0; i < item->as.enum_.variants.len; i++) {
        const Variant *v = &item->as.enum_.variants.items[i];
        declare_item(r, (Def){.kind = DEF_VARIANT, .name = v->name, .span = v->span,
                              .item = item, .is_pub = true, .target = owner.id, .variant = i});
    }
}

static void collect_item(Resolver *r, const Item *item) {
    if ((item->kind == ITEM_STRUCT || item->kind == ITEM_TUPLE_STRUCT) && collect_lang(r, item)) {
        return;
    }
    switch (item->kind) {
    case ITEM_FN:
        declare_fn(r, item, item->as.fn, true);
        break;
    case ITEM_EXTERN:
        for (uint32_t i = 0; i < item->as.extern_.fns.len; i++) {
            declare_fn(r, item, item->as.extern_.fns.items[i], true);
        }
        break;
    case ITEM_IMPORT:   // bound once every module's declarations are known
        break;
    default:
        declare_item(r, (Def){.kind = item_def_kind(item->kind), .name = item->name,
                              .span = item->span, .item = item, .is_pub = item->is_pub});
        if (item->kind == ITEM_STRUCT || item->kind == ITEM_TUPLE_STRUCT ||
            item->kind == ITEM_ENUM) {
            check_members(r, item);
        }
        DefId *slot = item->kind == ITEM_STRUCT || item->kind == ITEM_ENUM
                          ? named_lang(r, find_attribute(r->sema, item->attributes, "lang"))
                          : NULL;
        if (slot) {
            *slot = scope_module(&r->sema->scope, item->name);
        }
        if (slot && item->kind == ITEM_ENUM) {
            declare_variants(r, item, *slot);
        }
        break;
    }
}

static void resolve_item(Resolver *r, Item *item) {
    switch (item->kind) {
    case ITEM_FN:
        resolve_fn(r, item->as.fn);
        break;
    case ITEM_STRUCT: case ITEM_TUPLE_STRUCT:
        r->owner = item;
        for (uint32_t i = 0; i < item->as.struct_.methods.len; i++) {
            resolve_fn(r, item->as.struct_.methods.items[i]);
        }
        r->owner = NULL;
        for (uint32_t i = 0; i < item->as.struct_.fields.len; i++) {
            Expr *value = item->as.struct_.fields.items[i].default_value;
            if (value) {
                resolve_expr(r, value);
            }
        }
        break;
    case ITEM_ENUM:
        r->owner = item;
        for (uint32_t i = 0; i < item->as.enum_.methods.len; i++) {
            resolve_fn(r, item->as.enum_.methods.items[i]);
        }
        r->owner = NULL;
        break;
    case ITEM_CONST: case ITEM_GLOBAL:
        resolve_expr(r, item->as.const_.value);
        break;
    default:
        break;
    }
}

// `@test fn name() { ... }` (spec 12.2): `ofir test` calls it with nothing.
// Checked in every build, although only `ofir test` compiles the body.
static void check_test_signature(Resolver *r, const Item *item) {
    const FnDecl *fn = item->as.fn;
    if (fn->params.len > 0 || fn->ret || fn->type_params.len > 0) {
        resolve_error(r, "E0205", fn->span,
                      "a `@test` function takes no parameters and returns nothing");
    }
}

static void resolve_pass(Resolver *r, int pass) {
    ItemList items = r->sema->module->items;
    for (uint32_t i = 0; i < items.len; i++) {
        Item *item = items.items[i];
        if (pass == 0) {
            check_attributes(r, item);
        }
        if (pass == 0 && item->kind == ITEM_FN && item->attributes.len > 0 &&
            find_attribute(r->sema, item->attributes, "test")) {
            check_test_signature(r, item);
        }
        if (skipped_test(r->sema, item)) {
            continue;
        }
        if (pass == 0) {
            collect_item(r, item);
        } else if (pass == 1 && item->kind == ITEM_IMPORT) {
            resolve_import(r, item);
        } else if (pass == 2) {
            resolve_item(r, item);
        }
    }
}

// Three passes over every module: declarations, imports, then bodies, so
// that modules may refer to each other in any order (spec 7.2).
void resolve_program(Sema *sema) {
    Resolver r = {.sema = sema};
    for (uint32_t m = 0; sema->trace && m < sema->program.len; m++) {
        fprintf(sema->trace, "module %u " SV_FMT "\n", m, SV_ARG(sema->program.items[m].name));
    }
    r.self_name = intern(&sema->session->names, SV("self"));
    r.wildcard = intern(&sema->session->names, SV("_"));
    for (int pass = 0; pass < 3; pass++) {
        for (uint32_t m = 0; m < sema->program.len; m++) {
            sema_enter(sema, m);
            resolve_pass(&r, pass);
        }
        if (pass == 0) {
            import_prelude(&r);
        }
    }
    sema_enter(sema, 0);
    VEC_FREE(r.locals);
}
