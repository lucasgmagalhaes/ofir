// --emit=types: the declarations of the root module with their types, and
// the type of every local of its functions, which stage 1's checker must
// reproduce (roadmap 4.6). The move analysis does not run in this mode.

#include "sema/check_internal.h"
#include "sema/const_eval.h"

#include <inttypes.h>
#include <string.h>

static StrView name_of(Sema *sema, Symbol name) {
    return symbol_name(&sema->session->names, name);
}

static void type_line(Sema *sema, FILE *out, TypeId type) {
    StrBuf buf = {0};
    ty_text(&buf, &sema->types, type, sema_type_name, sema);
    fputs(buf.data ? buf.data : "", out);
    strbuf_free(&buf);
}

static void value_text(Sema *sema, FILE *out, const ConstValue *v) {
    switch (v->kind) {
    case CV_INT: {
        const Ty *ty = type_get(&sema->types, v->type);
        static const uint32_t BITS[] = {8, 16, 32, 64, 8, 16, 32, 64};
        uint32_t width = BITS[ty->data];
        if (ty->data <= INT_I64) {
            uint64_t bits = v->bits;
            if (width < 64 && (bits >> (width - 1)) & 1) {
                bits |= ~((1ull << width) - 1);
            }
            fprintf(out, "%" PRId64, (int64_t)bits);
        } else {
            fprintf(out, "%" PRIu64, v->bits);
        }
        break;
    }
    case CV_FLOAT: print_float_g(out, v->f); break;
    case CV_BOOL: fputs(v->b ? "true" : "false", out); break;
    case CV_STR: fprintf(out, "\"" SV_FMT "\"", SV_ARG(v->s)); break;
    }
}

static void fn_lines(Sema *sema, FILE *out, const char *owner, const FnDecl *fn) {
    const Def *def = sema_def(sema, fn->def);
    fprintf(out, "fn %s%s" SV_FMT ": ", owner ? owner : "", owner ? "." : "",
            SV_ARG(name_of(sema, fn->name)));
    if (def->typed) {
        type_line(sema, out, def->type);
    }
    fputc('\n', out);
    for (uint32_t i = 0; fn->local_types && i < fn->locals.len; i++) {
        fprintf(out, "  local %u " SV_FMT ": ", i, SV_ARG(name_of(sema, fn->locals.items[i].name)));
        type_line(sema, out, fn->local_types[i]);
        fputc('\n', out);
    }
}

static void methods_lines(Sema *sema, FILE *out, const Item *item, FnList methods) {
    char owner[128];
    snprintf(owner, sizeof owner, SV_FMT, SV_ARG(name_of(sema, item->name)));
    for (uint32_t i = 0; i < methods.len; i++) {
        if (!skipped_test(sema, item)) {
            fn_lines(sema, out, owner, methods.items[i]);
        }
    }
}

static void struct_lines(Sema *sema, FILE *out, const Item *item) {
    DefId id = scope_module(&sema->scope, item->name);
    const Def *def = id.id ? sema_def(sema, id) : NULL;
    if (def && def->item != item) {
        def = NULL;   // a `@lang` body, or a duplicate
    }
    for (uint32_t t = 0; !def && t < TID_BUILTIN_COUNT; t++) {
        if (sema->lang[t].id && sema_def(sema, sema->lang[t])->item == item) {
            def = sema_def(sema, sema->lang[t]);
        }
    }
    fprintf(out, "struct " SV_FMT " {", SV_ARG(name_of(sema, item->name)));
    for (uint32_t i = 0; def && def->fields && i < def->fields->count; i++) {
        fputs(i ? ", " : " ", out);
        if (def->fields->names[i].id) {
            fprintf(out, SV_FMT ": ", SV_ARG(name_of(sema, def->fields->names[i])));
        }
        type_line(sema, out, def->fields->types[i]);
    }
    fputs(" }\n", out);
    methods_lines(sema, out, item, item->as.struct_.methods);
}

static void enum_lines(Sema *sema, FILE *out, const Item *item) {
    DefId id = scope_module(&sema->scope, item->name);
    const Def *def = id.id ? sema_def(sema, id) : NULL;
    fprintf(out, "enum " SV_FMT " {", SV_ARG(name_of(sema, item->name)));
    const EnumInfo *info = def && def->item == item ? def->variants : NULL;
    for (uint32_t v = 0; info && v < info->count; v++) {
        const VariantInfo *variant = &info->variants[v];
        fprintf(out, "%s" SV_FMT, v ? ", " : " ", SV_ARG(name_of(sema, variant->name)));
        for (uint32_t i = 0; i < variant->count; i++) {
            fputs(i ? ", " : "(", out);
            type_line(sema, out, variant->types[i]);
        }
        fprintf(out, "%s = %" PRId64, variant->count ? ")" : "", variant->discriminant);
    }
    fputs(" }\n", out);
    methods_lines(sema, out, item, item->as.enum_.methods);
}

static void const_line(Sema *sema, FILE *out, const Item *item) {
    DefId id = scope_module(&sema->scope, item->name);
    const Def *def = id.id ? sema_def(sema, id) : NULL;
    fprintf(out, "%s " SV_FMT ": ", item->kind == ITEM_CONST ? "const" : "let",
            SV_ARG(name_of(sema, item->name)));
    if (def && def->item == item && def->typed) {
        type_line(sema, out, def->type);
        if (def->value && item->kind == ITEM_CONST) {   // not a global's initial value
            fputs(" = ", out);
            value_text(sema, out, def->value);
        }
    }
    fputc('\n', out);
}

void trace_types(Sema *sema, FILE *out) {
    ItemList items = sema->program.items[0].module->items;
    for (uint32_t i = 0; i < items.len; i++) {
        const Item *item = items.items[i];
        if (skipped_test(sema, item)) {
            continue;
        }
        switch (item->kind) {
        case ITEM_FN: fn_lines(sema, out, NULL, item->as.fn); break;
        case ITEM_EXTERN:
            for (uint32_t f = 0; f < item->as.extern_.fns.len; f++) {
                fn_lines(sema, out, "extern", item->as.extern_.fns.items[f]);
            }
            break;
        case ITEM_STRUCT: case ITEM_TUPLE_STRUCT: struct_lines(sema, out, item); break;
        case ITEM_ENUM: enum_lines(sema, out, item); break;
        case ITEM_CONST: case ITEM_GLOBAL: const_line(sema, out, item); break;
        case ITEM_TYPE_ALIAS: case ITEM_IMPORT: break;
        }
    }
}
