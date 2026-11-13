// LLVM IR generation: types, names, globals and the module.

#include "codegen/llvm_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

const char *llvm_type(Gen *g, TypeId type) {
    const Ty *ty = type_get(g->types, type);
    switch (ty->kind) {
    case TY_ADT: return adt_type(g, type);
    case TY_PARAM: fatal("llvm_type: a type parameter survived monomorphization");
    case TY_TUPLE: return tuple_type(g, ty);
    case TY_REF: case TY_PTR: return "ptr";
    case TY_BOOL: return "i1";
    case TY_INT:
        switch (ty->data) {
        case INT_I8: case INT_U8: return "i8";
        case INT_I16: case INT_U16: return "i16";
        case INT_I32: case INT_U32: return "i32";
        default: return "i64";
        }
    case TY_FLOAT: return ty->data == 32 ? "float" : "double";
    case TY_STR: return "%ofir.str";
    case TY_FN: return "{ ptr, ptr }";   // the code and the environment
    default: return "void";   // (), ! and the types the checker rejects
    }
}

bool llvm_has_value(TypeId type) {
    return type.id != TID_UNIT && type.id != TID_NEVER && type.id != TID_ERROR;
}

StrView def_name(Gen *g, DefId def) {
    return symbol_name(&g->sema->session->names, sema_def(g->sema, def)->name);
}

// User functions are prefixed so they never clash with C symbols; `main`
// becomes the entry point called by the runtime.
bool is_c_function(const Def *def) {
    return def->fn && (def->fn->abi.len > 0 || (def->item && def->item->kind == ITEM_EXTERN));
}

void function_name(Gen *g, Out *out, DefId def, TypeId targs, bool is_main) {
    if (is_main) {
        out_str(out, "@ofir_main");
        return;
    }
    if (is_glue(def)) {   // `ofir.drop<T>`, `ofir.clone<T>`
        out_str(out, def.id == GLUE_DROP ? "@\"ofir.drop" : "@\"ofir.clone");
        type_args_text(g, out, targs);
        out_char(out, '"');
        return;
    }
    const Def *d = sema_def(g->sema, def);
    if (is_c_function(d)) {   // a C function keeps its name
        StrView name = def_name(g, def);
        out_char(out, '@');
        out_write(out, name.ptr, name.len);
        return;
    }
    out_str(out, "@\"ofir.");
    if (d->module != 0) {   // qualified by its module, except in the root module
        StrView module = g->sema->program.items[d->module].name;
        out_write(out, module.ptr, module.len);
        out_char(out, '.');
    }
    bool method = d->item && d->item->kind != ITEM_FN && d->item->kind != ITEM_EXTERN;
    if (method) {   // a method: qualified by its type, `ofir.Point.norm`
        StrView owner = symbol_name(&g->sema->session->names, d->item->name);
        out_write(out, owner.ptr, owner.len);
        out_char(out, '.');
    }
    StrView name = def_name(g, def);
    out_write(out, name.ptr, name.len);
    if (targs.id != TID_UNIT) {   // an instance: `ofir.identity<int>`
        type_args_text(g, out, targs);
    }
    out_char(out, '"');
}

// "%t<N>", written by hand: this is the most frequent formatting.
void next_temp(Gen *g, char text[16]) {
    char digits[12];
    int n = 0;
    uint32_t value = g->temp++;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    text[0] = '%';
    text[1] = 't';
    for (int i = 0; i < n; i++) {
        text[2 + i] = digits[n - 1 - i];
    }
    text[2 + n] = '\0';
}

static void escaped_bytes(Out *out, StrView text) {
    for (size_t i = 0; i < text.len; i++) {
        unsigned char c = (unsigned char)text.ptr[i];
        if (c < 0x20 || c >= 0x7f || c == '"' || c == '\\') {
            out_fmt(out, "\\%02X", c);
        } else {
            out_char(out, (char)c);
        }
    }
}

uint32_t string_global(Gen *g, StrView text) {
    uint32_t n = g->str_count++;
    out_fmt(&g->globals, "@.str.%u = private unnamed_addr constant [%zu x i8] c\"", n, text.len);
    escaped_bytes(&g->globals, text);
    out_str(&g->globals, "\"\n");
    return n;
}

// The name of a source file, for panic locations: `@.file.<FileId>`.
static void file_global(Gen *g, FileId file) {
    if (file.id < g->files_emitted.len && g->files_emitted.data[file.id]) {
        return;
    }
    while (g->files_emitted.len <= file.id) {
        VEC_PUSH(g->files_emitted, false);
    }
    g->files_emitted.data[file.id] = true;
    StrView path = source_file(&g->sema->session->sources, file)->path;
    out_fmt(&g->globals, "@.file.%u = private unnamed_addr constant [%zu x i8] c\"", file.id,
            path.len + 1);
    escaped_bytes(&g->globals, path);
    out_str(&g->globals, "\\00\"\n");
}

uint32_t location_global(Gen *g, Span span) {
    LineCol pos = source_line_col(&g->sema->session->sources, span.file, span.start);
    file_global(g, span.file);
    uint32_t n = g->loc_count++;
    out_fmt(&g->globals, "@.loc.%u = private unnamed_addr constant %%ofir.loc "
            "{ ptr @.file.%u, i32 %u, i32 %u }\n", n, span.file.id, pos.line, pos.column);
    return n;
}

static const char RUNTIME_DECLS[] =
    "declare void @ofir_print_str(ptr) nounwind\n"
    "declare void @ofir_print_int(i64) nounwind\n"
    "declare void @ofir_print_uint(i64) nounwind\n"
    "declare void @ofir_print_float(double) nounwind\n"
    "declare void @ofir_print_f32(float) nounwind\n"
    "declare void @ofir_print_bool(i1 zeroext) nounwind\n"
    "declare void @ofir_print_newline() nounwind\n"
    "declare i64 @ofir_test_index() nounwind\n"
    "declare void @ofir_str_push_float(ptr, double) nounwind\n"
    "declare void @ofir_str_push_f32(ptr, float) nounwind\n"
    "declare void @ofir_panic_overflow(ptr) noreturn cold nounwind\n"
    "declare void @ofir_panic_div_zero(ptr) noreturn cold nounwind\n"
    "declare void @ofir_panic_shift(ptr) noreturn cold nounwind\n"
    "declare void @ofir_panic_cast(ptr) noreturn cold nounwind\n"
    "declare void @ofir_panic_bounds(i64, i64, ptr) noreturn cold nounwind\n"
    "declare void @ofir_panic_assert(ptr) noreturn cold nounwind\n"
    "declare void @ofir_panic_str(ptr, ptr) noreturn cold nounwind\n";

static void overflow_decls(Gen *g) {
    static const char *const OPS[] = {"add", "sub", "mul"};
    static const unsigned BITS[] = {8, 16, 32, 64};
    for (int i = 0; i < 2 * 3 * 4; i++) {
        int s = i / 12, op = i / 4 % 3, b = i % 4;
        if (g->overflow_used[s][op][b]) {
            out_fmt(&g->out, "declare { i%u, i1 } @llvm.%c%s.with.overflow.i%u(i%u, i%u)\n",
                    BITS[b], s ? 's' : 'u', OPS[op], BITS[b], BITS[b], BITS[b]);
        }
    }
}

Gen *llvm_begin(Sema *sema, FILE *file) {
    Gen *g = xcalloc(1, sizeof(Gen));
    g->sema = sema;
    g->types = &sema->types;
    out_to_file(&g->out, file);
    out_to_memory(&g->globals);
    out_to_memory(&g->type_defs);
    out_to_memory(&g->fn_text);
    FileId root = sema->program.items[0].module->file;
    const SourceFile *source = source_file(&sema->session->sources, root);
    out_to_memory(&g->path);
    escaped_bytes(&g->path, source->path);
    out_char(&g->path, '\0');
    out_fmt(&g->out, "; Generated by the Ofir stage 0 compiler.\nsource_filename = \"%s\"\n\n",
            g->path.buf);
    out_str(&g->out, "%ofir.str = type { ptr, i64, i64 }\n");
    out_str(&g->out, "%ofir.loc = type { ptr, i32, i32 }\n");
    // Every struct is known after type checking. The definitions come first:
    // LLVM needs a type's size when a function allocates it.
    // Instances of generic types are defined as functions use them.
    for (size_t i = 1; i < sema->defs.len; i++) {
        const Def *def = &sema->defs.data[i];
        bool known = (def->kind == DEF_STRUCT && def->fields && !def->lang) ||
                     (def->kind == DEF_ENUM && def->variants);
        if (known && item_type_params(def->item).len == 0) {
            adt_type(g, type_intern(g->types, (Ty){.kind = TY_ADT, .data = (uint32_t)i}));
        }
    }
    out_write(&g->out, g->type_defs.buf, g->type_defs.len);
    out_char(&g->out, '\n');
    g->type_defs.len = 0;
    return g;
}

// LLVM needs a struct type's definition before an instruction that sizes
// it, and the instances of generic types are defined as functions use
// them. So a function is generated into a buffer; the types it defined are
// written first, then the function.
void llvm_function(Gen *g, const AirBody *body) {
    Out file = g->out;
    g->out = g->fn_text;
    g->out.len = 0;
    gen_body(g, body);
    g->fn_text = g->out;
    g->out = file;
    if (g->type_defs.len > 0) {
        out_write(&g->out, g->type_defs.buf, g->type_defs.len);
        out_char(&g->out, '\n');
        g->type_defs.len = 0;
    }
    out_write(&g->out, g->fn_text.buf, g->fn_text.len);
}

void mark_c_used(Gen *g, DefId def) {
    while (g->c_used.len <= def.id) {
        VEC_PUSH(g->c_used, false);
    }
    g->c_used.data[def.id] = true;
}

// Whether a C function with the name of `id` is already declared: several
// modules may declare the same C function.
static bool declared_before(Gen *g, uint32_t id) {
    StrView name = def_name(g, (DefId){id});
    for (uint32_t other = 0; other < id; other++) {
        if (g->c_used.data[other] && sv_eq(def_name(g, (DefId){other}), name)) {
            return true;
        }
    }
    return false;
}

// `declare` for each function of an extern block that is called.
static void c_decls(Gen *g) {
    for (uint32_t id = 0; id < g->c_used.len; id++) {
        if (!g->c_used.data[id] || declared_before(g, id)) {
            continue;
        }
        const Ty *sig = type_get(g->types, sema_def(g->sema, (DefId){id})->type);
        const char *ret = llvm_type(g, sig->inner);
        out_fmt(&g->out, "declare %s%s ", strcmp(ret, "i1") ? "" : "zeroext ", ret);
        function_name(g, &g->out, (DefId){id}, type_id(TID_UNIT), false);
        out_char(&g->out, '(');
        for (uint32_t i = 0; i < sig->arg_count; i++) {
            const char *type = llvm_type(g, sig->args[i]);
            out_fmt(&g->out, "%s%s%s", i ? ", " : "", type, strcmp(type, "i1") ? "" : " zeroext");
        }
        out_str(&g->out, ") nounwind\n");
    }
}

void llvm_end(Gen *g) {
    out_str(&g->out, RUNTIME_DECLS);
    overflow_decls(g);
    gen_float_decls(g);
    c_decls(g);
    out_char(&g->out, '\n');
    out_write(&g->out, g->globals.buf, g->globals.len);
    out_free(&g->type_defs);
    out_free(&g->fn_text);
    free(g->adt_types);
    free(g->slots);
    out_free(&g->path);
    out_free(&g->globals);
    VEC_FREE(g->files_emitted);
    VEC_FREE(g->c_used);
    out_free(&g->out);
    free(g);
}
