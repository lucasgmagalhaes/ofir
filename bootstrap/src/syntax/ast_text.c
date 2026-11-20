// Compact, source-like text for types, patterns and literals, used by the
// AST printer (types and patterns are printed on one line).

#include "syntax/ast_text.h"

#include <stdlib.h>
#include <string.h>

static void symbols_text(StrBuf *out, SymbolList path, const Interner *names) {
    for (uint32_t i = 0; i < path.len; i++) {
        if (i > 0) {
            strbuf_push(out, '.');
        }
        strbuf_append(out, symbol_name(names, path.items[i]));
    }
}

static void type_list_text(StrBuf *out, TypeList types, const Interner *names) {
    for (uint32_t i = 0; i < types.len; i++) {
        strbuf_append_cstr(out, i > 0 ? ", " : "");
        type_text(out, types.items[i], names);
    }
}

static void fn_type_text(StrBuf *out, const TypeExpr *t, const Interner *names) {
    strbuf_append_cstr(out, t->as.fn.owned ? "move fn(" : "fn(");
    for (uint32_t i = 0; i < t->as.fn.params.len; i++) {
        strbuf_append_cstr(out, i > 0 ? ", " : "");
        Mode mode = t->as.fn.modes.items[i];
        strbuf_append_cstr(out, mode == MODE_MUT ? "mut " : mode == MODE_MOVE ? "move " : "");
        type_text(out, t->as.fn.params.items[i], names);
    }
    strbuf_push(out, ')');
    if (t->as.fn.ret) {
        strbuf_push(out, ' ');
        type_text(out, t->as.fn.ret, names);
    }
}

void type_text(StrBuf *out, const TypeExpr *t, const Interner *names) {
    switch (t->kind) {
    case TYPE_PATH:
        symbols_text(out, t->as.path.segments, names);
        if (t->as.path.args.len) {
            strbuf_push(out, '<');
            type_list_text(out, t->as.path.args, names);
            strbuf_push(out, '>');
        }
        break;
    case TYPE_SELF: strbuf_append_cstr(out, "Self"); break;
    case TYPE_REF:
    case TYPE_PTR:
        strbuf_append_cstr(out, t->kind == TYPE_REF ? "&" : "*");
        strbuf_append_cstr(out, t->as.ref.mutable ? "mut " : "");
        type_text(out, t->as.ref.inner, names);
        break;
    case TYPE_SLICE:
    case TYPE_ARRAY:
        strbuf_push(out, '[');
        type_text(out, t->as.array.elem, names);
        if (t->as.array.len) {
            strbuf_append_cstr(out, "; ");
            literal_text(out, t->as.array.len, names);
        }
        strbuf_push(out, ']');
        break;
    case TYPE_TUPLE:
        strbuf_push(out, '(');
        type_list_text(out, t->as.tuple, names);
        strbuf_append_cstr(out, t->as.tuple.len == 1 ? ",)" : ")");
        break;
    case TYPE_FN: fn_type_text(out, t, names); break;
    case TYPE_DYN:
        strbuf_append_cstr(out, "dyn ");
        type_text(out, t->as.inner, names);
        break;
    case TYPE_OPTION:
        type_text(out, t->as.inner, names);
        strbuf_push(out, '?');
        break;
    case TYPE_NEVER: strbuf_push(out, '!'); break;
    }
}

static void escaped_text(StrBuf *out, StrView s) {
    strbuf_push(out, '"');
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.ptr[i];
        if (c == '\n') {
            strbuf_append_cstr(out, "\\n");
        } else if (c == '"' || c == '\\' || c == '{' || c == '}') {
            strbuf_push(out, '\\');
            strbuf_push(out, (char)c);
        } else if (c < 0x20 || c == 0x7f) {
            strbuf_appendf(out, "\\x%02x", c);
        } else {
            strbuf_push(out, (char)c);
        }
    }
    strbuf_push(out, '"');
}

// The shortest decimal form that reads back as the same value.
static void float_text(StrBuf *out, double value) {
    char text[32];
    for (int precision = 1; precision <= 17; precision++) {
        snprintf(text, sizeof text, "%.*g", precision, value);
        if (strtod(text, NULL) == value) {
            break;
        }
    }
    strbuf_append_cstr(out, text);
    if (!strpbrk(text, ".eEni")) {
        strbuf_append_cstr(out, ".0");   // keep `1.0` distinct from the integer `1`
    }
}

void literal_text(StrBuf *out, const Expr *e, const Interner *names) {
    if (!e) {   // a range pattern whose end is not a literal (a parse error)
        strbuf_append_cstr(out, "<expr>");
        return;
    }
    switch (e->kind) {
    case EXPR_INT: strbuf_appendf(out, "%llu", (unsigned long long)e->as.int_value); break;
    case EXPR_FLOAT: float_text(out, e->as.float_value); break;
    case EXPR_BYTE: strbuf_appendf(out, "b%u", e->as.byte_value); break;
    case EXPR_BOOL: strbuf_append_cstr(out, e->as.bool_value ? "true" : "false"); break;
    case EXPR_STR: escaped_text(out, e->as.str); break;
    case EXPR_IDENT: strbuf_append(out, symbol_name(names, e->as.ident.name)); break;
    case EXPR_UNARY:
        strbuf_push(out, '-');
        literal_text(out, e->as.unary.operand, names);
        break;
    default: strbuf_append_cstr(out, "<expr>"); break;
    }
}

static void pattern_list_text(StrBuf *out, PatternList list, const Interner *names) {
    for (uint32_t i = 0; i < list.len; i++) {
        strbuf_append_cstr(out, i > 0 ? ", " : "");
        pattern_text(out, list.items[i], names);
    }
}

static void struct_pattern_text(StrBuf *out, const Pattern *p, const Interner *names) {
    symbols_text(out, p->as.struct_.path, names);
    strbuf_push(out, '{');
    for (uint32_t i = 0; i < p->as.struct_.fields.len; i++) {
        const FieldPattern *f = &p->as.struct_.fields.items[i];
        strbuf_append_cstr(out, i > 0 ? ", " : "");
        strbuf_append(out, symbol_name(names, f->name));
        if (f->pattern) {
            strbuf_append_cstr(out, ": ");
            pattern_text(out, f->pattern, names);
        }
    }
    const char *end = p->as.struct_.fields.len ? ", ..}" : "..}";
    strbuf_append_cstr(out, p->as.struct_.rest ? end : "}");
}

void pattern_text(StrBuf *out, const Pattern *p, const Interner *names) {
    switch (p->kind) {
    case PAT_WILDCARD: strbuf_push(out, '_'); break;
    case PAT_BINDING:
        strbuf_append_cstr(out, p->as.binding.mutable ? "mut " : "");
        strbuf_append(out, symbol_name(names, p->as.binding.name));
        break;
    case PAT_LITERAL: literal_text(out, p->as.literal, names); break;
    case PAT_RANGE:
        literal_text(out, p->as.range.lo, names);
        strbuf_append_cstr(out, p->as.range.inclusive ? "..=" : "..");
        literal_text(out, p->as.range.hi, names);
        break;
    case PAT_VARIANT:
        symbols_text(out, p->as.variant.path, names);
        strbuf_push(out, '.');
        strbuf_append(out, symbol_name(names, p->as.variant.name));
        if (p->as.variant.has_args) {
            strbuf_push(out, '(');
            pattern_list_text(out, p->as.variant.args, names);
            strbuf_push(out, ')');
        }
        break;
    case PAT_TUPLE:
        strbuf_push(out, '(');
        pattern_list_text(out, p->as.tuple, names);
        strbuf_push(out, ')');
        break;
    case PAT_STRUCT: struct_pattern_text(out, p, names); break;
    case PAT_TUPLE_STRUCT:
        symbols_text(out, p->as.tuple_struct.path, names);
        strbuf_push(out, '(');
        pattern_list_text(out, p->as.tuple_struct.args, names);
        strbuf_push(out, ')');
        break;
    case PAT_OR:
        for (uint32_t i = 0; i < p->as.alternatives.len; i++) {
            strbuf_append_cstr(out, i > 0 ? " | " : "");
            pattern_text(out, p->as.alternatives.items[i], names);
        }
        break;
    }
}
