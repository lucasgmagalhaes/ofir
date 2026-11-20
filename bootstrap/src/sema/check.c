// Type checking (docs/design/compiler-architecture.md, 6): signatures first,
// then constants and function bodies, one function at a time.

#include "sema/check_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void check_error(Checker *c, const char *code, Span span, const char *format, ...) {
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    diag_error(&c->sema->session->diags, code, span, "%s", message);
}

TypeId unsupported(Checker *c, Span span, const char *what) {
    check_error(c, "E0901", span, "%s not supported yet", what);
    return type_id(TID_ERROR);
}

const char *type_name(Checker *c, TypeId type) {
    static StrBuf buf;
    strbuf_clear(&buf);
    ty_text(&buf, c->types, type, sema_type_name, c->sema);
    return buf.data;
}

// Without owned values nothing can be moved, uninitialized or dropped. A
// std function waits until the lowering reaches it (sema_moves_late).
static void analyze_moves(Checker *c, FnDecl *fn) {
    if (!c->owned_values) {
        return;
    }
    StrView module = c->sema->program.items[c->sema->current].name;
    if (module.len >= 3 && memcmp(module.ptr, "std", 3) == 0) {
        fn->moves_pending = true;
    } else {
        check_moves(c, fn);
    }
}

static bool in_std_module(Checker *c) {
    StrView module = c->sema->program.items[c->sema->current].name;
    return module.len >= 3 && memcmp(module.ptr, "std", 3) == 0;
}

static void check_body(Checker *c, FnDecl *fn, TypeId self_type);

static void check_fn(Checker *c, FnDecl *fn, TypeId self_type) {
    Generics outer = enter_generics(c, (Generics){.owner = c->generics.owner,
                                                  .owner_bounds = c->generics.owner_bounds,
                                                  .fn = fn->type_params,
                                                  .fn_bounds = fn->bounds});
    check_bound_names(c, fn->bounds);
    const Ty *sig = type_get(c->types, fn_signature(c, fn));
    check_borrow_source(c, fn, sig, self_type);   // what callers need, now
    enter_generics(c, outer);
    // The body of a std function waits until the lowering reaches it: a
    // program uses few of them. The root module is always checked whole.
    if (fn->body && c->sema->current != 0 && in_std_module(c)) {
        fn->check_pending = true;
        fn->pending_self = self_type;
        fn->pending_owner_params = c->generics.owner;
        fn->pending_owner_bounds = c->generics.owner_bounds;
        return;
    }
    check_body(c, fn, self_type);
}

static void check_body(Checker *c, FnDecl *fn, TypeId self_type) {
    Generics outer = enter_generics(c, (Generics){.owner = c->generics.owner,
                                                  .owner_bounds = c->generics.owner_bounds,
                                                  .fn = fn->type_params,
                                                  .fn_bounds = fn->bounds});
    const Ty *sig = type_get(c->types, fn_signature(c, fn));
    c->fn = fn;
    c->ret = sig->inner;
    c->self_type = self_type;
    c->local_types = ARENA_ARRAY(&c->sema->session->arena, TypeId, fn->locals.len + 1);
    fn->local_types = c->local_types;
    if (fn->has_self) {
        c->local_types[0] = self_type;
    }
    for (uint32_t i = 0; i < fn->params.len; i++) {
        c->local_types[fn->params.items[i].local.id] = sig->args[i];
    }
    // GCOVR_EXCL_START: only extern block functions lack a body; check_extern takes them
    if (!fn->body) {
        enter_generics(c, outer);
        return;
    }
    // GCOVR_EXCL_STOP
    c->unsafe_depth = fn->is_unsafe ? 1 : 0;
    uint32_t errors = c->sema->session->diags.error_count;
    note_signature(c, fn, sig, self_type);
    TypeId body = check_block(c, fn->body, NO_TYPE, false);
    c->unsafe_depth = 0;
    bool needs_value = c->ret.id != TID_UNIT && c->ret.id != TID_ERROR;
    if (needs_value && body.id != TID_NEVER) {
        check_error(c, "E0303", fn->span,
                    "function may reach its end without returning a value");
    }
    if (c->sema->session->diags.error_count == errors) {   // no cascade after type errors
        analyze_moves(c, fn);
    }
    enter_generics(c, outer);
}

static void check_methods(Checker *c, const Item *item, FnList methods) {
    TypeId self_type = owner_type(c->sema, item);
    Generics outer = enter_generics(c, (Generics){.owner = item_type_params(item),
                                                  .owner_bounds = item->bounds});
    check_bound_names(c, item->bounds);
    for (uint32_t i = 0; i < methods.len; i++) {
        check_fn(c, methods.items[i], self_type);
    }
    enter_generics(c, outer);
}

// `extern "C"` blocks cross the C boundary; `extern "intrinsic"` blocks,
// reserved to std, declare operations the compiler generates.
static void check_extern(Checker *c, Item *item) {
    StrView abi = item->as.extern_.abi;
    if (sv_eq(abi, SV("intrinsic"))) {
        StrView module = c->sema->program.items[c->sema->current].name;
        if (module.len < 3 || memcmp(module.ptr, "std", 3) != 0) {
            check_error(c, "E0805", item->span, "intrinsics are reserved to the standard library");
        }
        for (uint32_t i = 0; i < item->as.extern_.fns.len; i++) {
            fn_signature(c, item->as.extern_.fns.items[i]);
        }
        return;
    }
    if (!sv_eq(abi, SV("C"))) {
        check_error(c, "E0804", item->span, "unknown ABI: only `extern \"C\"` is supported");
    }
    for (uint32_t i = 0; i < item->as.extern_.fns.len; i++) {
        check_c_signature(c, item->as.extern_.fns.items[i]);
    }
}

static void check_item(Checker *c, Item *item) {
    switch (item->kind) {
    case ITEM_FN:
        if (item->as.fn->abi.len > 0) {   // `extern "C" fn` with a body, called from C
            check_c_signature(c, item->as.fn);
        }
        check_fn(c, item->as.fn, NO_TYPE);
        break;
    case ITEM_EXTERN: check_extern(c, item); break;
    case ITEM_STRUCT: case ITEM_TUPLE_STRUCT: {
        TypeId owner = owner_type(c->sema, item);
        Def *lang = lang_def(c->sema, owner);
        struct_fields(c, lang ? c->sema->lang[owner.id] : (DefId){type_get(c->types, owner)->data});
        if (lang) {
            check_lang_layout(c, lang, owner);
        }
        if (type_get(c->types, owner)->kind == TY_ADT &&
            type_get(c->types, owner)->data == c->sema->list.id) {
            check_list_layout(c, sema_def(c->sema, c->sema->list));
        }
        check_methods(c, item, item->as.struct_.methods);
        break;
    }
    case ITEM_ENUM: {
        DefId id = scope_module(&c->sema->scope, item->name);
        enum_variants(c, id);
        if (id.id == c->sema->option.id || id.id == c->sema->result.id) {
            check_wrapper_layout(c, id);
        }
        check_methods(c, item, item->as.enum_.methods);
        break;
    }
    case ITEM_CONST: case ITEM_GLOBAL:
        check_const(c, sema_def(c->sema, scope_module(&c->sema->scope, item->name)));
        break;
    case ITEM_TYPE_ALIAS: case ITEM_IMPORT:
        break;
    }
}

void sema_check_late(Sema *sema, FnDecl *fn) {
    if (!fn->check_pending) {
        return;
    }
    fn->check_pending = false;
    uint32_t saved = sema_enter(sema, sema_def(sema, fn->def)->module);
    Checker c = {.sema = sema, .types = &sema->types, .self_type = NO_TYPE};
    c.generics.owner = fn->pending_owner_params;
    c.generics.owner_bounds = fn->pending_owner_bounds;
    check_body(&c, fn, fn->pending_self);
    VEC_FREE(c.loops);
    arena_free(&c.scratch);
    sema_enter(sema, saved);
}

void sema_moves_late(Sema *sema, FnDecl *fn) {
    if (!fn->moves_pending) {
        return;
    }
    fn->moves_pending = false;
    Checker c = {.sema = sema, .types = &sema->types, .self_type = NO_TYPE, .fn = fn,
                 .local_types = fn->local_types};
    check_moves(&c, fn);
    arena_free(&c.scratch);
}

void check_program(Sema *sema) {
    Checker c = {.sema = sema, .types = &sema->types, .self_type = NO_TYPE};
    for (uint32_t m = 0; m < sema->program.len; m++) {
        sema_enter(sema, m);
        for (uint32_t i = 0; i < sema->module->items.len; i++) {
            if (!skipped_test(sema, sema->module->items.items[i])) {
                check_item(&c, sema->module->items.items[i]);
            }
        }
    }
    sema_enter(sema, 0);
    warn_unused_imports(sema);
    VEC_FREE(c.loops);
    arena_free(&c.scratch);
}
