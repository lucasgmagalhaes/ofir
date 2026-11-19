// Token names and the `--emit=tokens` format.

#include "syntax/token.h"

#include <stdlib.h>

static const char *const token_texts[] = {
#define X(name, text) text,
    TOKEN_KINDS(X)
#undef X
};

const char *token_kind_text(TokenKind kind) {
    return token_texts[kind];
}

static void print_escaped_byte(unsigned char c, FILE *out) {
    switch (c) {
    case '\n': fputs("\\n", out); break;
    case '\t': fputs("\\t", out); break;
    case '\r': fputs("\\r", out); break;
    case '\\': fputs("\\\\", out); break;
    case '"': fputs("\\\"", out); break;
    case '{': fputs("\\{", out); break;
    case '}': fputs("\\}", out); break;
    default:
        if (c < 0x20 || c == 0x7f) {
            fprintf(out, "\\x%02x", c);
        } else {
            fputc((char)c, out);
        }
    }
}

static void print_escaped(StrView text, FILE *out) {
    fputc('"', out);
    for (size_t i = 0; i < text.len; i++) {
        print_escaped_byte((unsigned char)text.ptr[i], out);
    }
    fputc('"', out);
}

// Shortest representation that reads back to the same value.
static void print_float(double value, FILE *out) {
    char buf[32];
    for (int precision = 1; precision <= 17; precision++) {
        snprintf(buf, sizeof buf, "%.*g", precision, value);
        if (strtod(buf, NULL) == value) {
            break;
        }
    }
    fputs(buf, out);
}

static void print_value(const Token *t, const Interner *names, FILE *out) {
    switch (t->kind) {
    case TOK_IDENT:
        fprintf(out, " " SV_FMT, SV_ARG(symbol_name(names, t->as.symbol)));
        break;
    case TOK_INT:
        fprintf(out, " %llu", (unsigned long long)t->as.int_value);
        break;
    case TOK_FLOAT:
        fputc(' ', out);
        print_float(t->as.float_value, out);
        break;
    case TOK_BYTE:
        fprintf(out, " %u", t->as.byte_value);
        break;
    case TOK_STR: case TOK_STR_START: case TOK_STR_MID: case TOK_STR_END:
    case TOK_DOC_COMMENT: case TOK_MODULE_DOC:
        fputc(' ', out);
        print_escaped(*t->as.text, out);
        break;
    default:
        break;
    }
}

void print_tokens(const TokenList *tokens, const SourceMap *map, const Interner *names, FILE *out) {
    for (size_t i = 0; i < tokens->len; i++) {
        const Token *t = &tokens->data[i];
        LineCol lc = source_line_col(map, t->span.file, t->span.start);
        fprintf(out, "%u:%u %s", lc.line, lc.column, token_kind_text(t->kind));
        print_value(t, names, out);
        fputc('\n', out);
    }
}
