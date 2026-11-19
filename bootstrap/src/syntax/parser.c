// Parser core: the token stream, newlines, errors and recovery, and the
// entry point. Each grammar area has its own file (parse_*.c).

#include "syntax/parser_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Tokens

// Moves the lexer's next tokens into the chunks. When the lexer reports an
// error the rest of the file is lexed (for its errors) and the stream ends.
static void pull(TokenStream *ts) {
    Diagnostics *diags = &ts->lexer.session->diags;
    uint32_t errors = diags->error_count;
    ts->staged.len = 0;
    lex_step(&ts->lexer, LEX_BYTES);
    if (diags->error_count != errors && !ts->failed) {
        ts->failed = true;
        while (!ts->lexer.done) {   // keeps only the EOF token
            ts->staged.len = 0;
            lex_step(&ts->lexer, LEX_BYTES);
        }
    }
    const Token *from = ts->staged.data;
    size_t left = ts->staged.len;
    while (left > 0) {
        size_t offset = ts->count & (TOKEN_CHUNK - 1);
        if (offset == 0) {
            VEC_PUSH(ts->chunks, malloc(TOKEN_CHUNK * sizeof(Token)));
        }
        size_t n = TOKEN_CHUNK - offset < left ? TOKEN_CHUNK - offset : left;
        memcpy(&ts->chunks.data[ts->count >> TOKEN_CHUNK_BITS][offset], from, n * sizeof(Token));
        ts->count += n;
        from += n;
        left -= n;
    }
    ts->ready = ts->failed ? 0 : ts->count;
}

const Token *token_pull(const Parser *p, size_t index) {
    TokenStream *ts = p->tokens;
    while (index >= ts->count && !ts->lexer.done) {
        pull(ts);
    }
    if (index >= ts->count || ts->failed) {
        index = ts->count - 1;   // the last token is EOF
    }
    return &ts->chunks.data[index >> TOKEN_CHUNK_BITS][index & (TOKEN_CHUNK - 1)];
}

TokenKind peek_at(const Parser *p, size_t ahead) {
    return peek_token(p, ahead)->kind;
}

const Token *advance(Parser *p) {
    const Token *t = peek_token(p, 0);
    if (t->kind != TOK_EOF) {
        p->pos++;
    }
    return t;
}

bool accept(Parser *p, TokenKind kind) {
    if (at(p, kind)) {
        advance(p);
        return true;
    }
    return false;
}

bool at_word(const Parser *p, Symbol word) {
    const Token *t = peek_token(p, 0);
    return t->kind == TOK_IDENT && symbol_eq(t->as.symbol, word);
}

// Newlines are skipped wherever the grammar needs more tokens (spec 1.3).
void skip_newlines(Parser *p) {
    while (at(p, TOK_NEWLINE)) {
        advance(p);
    }
}

Span span_since(const Parser *p, Span start) {
    const Token *previous = token_index(p, p->pos > 0 ? p->pos - 1 : 0);
    uint32_t end = previous->span.end > start.start ? previous->span.end : start.end;
    return (Span){start.file, start.start, end};
}

void *parser_alloc(Parser *p, size_t size) {
    return arena_alloc(&p->session->arena, size, _Alignof(max_align_t));
}

// ---------------------------------------------------------------------------
// Errors

static void record_error(Parser *p, const char *code, Span span, const char *message) {
    SyntaxError error = {code, span, arena_strndup(&p->session->arena, message, strlen(message))};
    VEC_PUSH(p->errors, error);
}

void syntax_error(Parser *p, Span span, const char *format, ...) {
    if (p->recovering || p->too_deep) {
        return;   // one error per statement: the rest is usually a consequence
    }
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    record_error(p, "E0100", span, message);
    p->recovering = true;
}

bool nest(Parser *p) {
    if (++p->depth <= MAX_NESTING && !p->too_deep) {
        return true;
    }
    if (!p->too_deep) {
        char message[64];
        snprintf(message, sizeof message, "nesting too deep: more than %d levels", MAX_NESTING);
        record_error(p, "E0101", peek_token(p, 0)->span, message);
        p->too_deep = true;
    }
    p->recovering = true;
    while (peek(p) != TOK_EOF) {   // to the end of the file
        p->pos++;
    }
    return false;
}

static const char *describe(const Token *t) {
    switch (t->kind) {
    case TOK_IDENT: return "an identifier";
    case TOK_INT: case TOK_FLOAT: case TOK_BYTE: return "a number";
    case TOK_STR: case TOK_STR_START: return "a string";
    case TOK_NEWLINE: return "the end of the line";
    case TOK_EOF: return "the end of the file";
    default: return NULL;
    }
}

// Where to report a missing token: at the token found, or, for the end of
// a line, just after the previous token (not after a trailing comment).
static Span error_span(const Parser *p, const Token *t) {
    if (t->kind != TOK_NEWLINE || p->pos == 0) {
        return t->span;
    }
    Span after = token_index(p, p->pos - 1)->span;
    after.start = after.end;
    return after;
}

void unexpected(Parser *p, const char *what) {
    const Token *t = peek_token(p, 0);
    const char *found = describe(t);
    Span span = error_span(p, t);
    if (found) {
        syntax_error(p, span, "expected %s, found %s", what, found);
    } else {
        syntax_error(p, span, "expected %s, found `%s`", what, token_kind_text(t->kind));
    }
}

const Token *expect(Parser *p, TokenKind kind, const char *what) {
    if (at(p, kind)) {
        return advance(p);
    }
    unexpected(p, what);
    return NULL;
}

bool accept_separator(Parser *p) {
    bool comma = accept(p, TOK_COMMA);
    bool newline = accept(p, TOK_NEWLINE);
    return comma || newline;
}

void expect_terminator(Parser *p) {
    if (accept(p, TOK_NEWLINE)) {
        skip_newlines(p);
    } else if (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
        unexpected(p, "a new line after the statement");
    }
}

// Skips to the end of the current statement: a newline outside brackets, or
// the `}` that closes the enclosing block (not consumed).
void synchronize(Parser *p) {
    int depth = 0;
    while (!at(p, TOK_EOF)) {
        TokenKind k = peek(p);
        if (depth == 0 && (k == TOK_NEWLINE || k == TOK_RBRACE)) {
            break;
        }
        if (k == TOK_LPAREN || k == TOK_LBRACKET || k == TOK_LBRACE) {
            depth++;
        } else if ((k == TOK_RPAREN || k == TOK_RBRACKET || k == TOK_RBRACE) && depth > 0) {
            depth--;
        }
        advance(p);
    }
    skip_newlines(p);
    p->recovering = false;
}
