// Definitions shared by the files of the lexer (lexer.c, literals.c).

#ifndef OFIR_LEXER_INTERNAL_H
#define OFIR_LEXER_INTERNAL_H

#include "syntax/lexer.h"

typedef struct {
    Session *session;
    const char *src;
    uint32_t len;
    uint32_t pos;
    FileId file;
    TokenList *out;
    Vec(char) brackets;            // open `(`, `[` and `{`, innermost last
    Vec(uint32_t) interpolations;  // brackets.len when each interpolation opened
} Lexer;

static inline char peek(const Lexer *lx, uint32_t offset) {
    uint32_t i = lx->pos + offset;
    return i < lx->len ? lx->src[i] : '\0';
}

static inline bool at_end(const Lexer *lx) {
    return lx->pos >= lx->len;
}

static inline Span span_from(const Lexer *lx, uint32_t start) {
    return (Span){lx->file, start, lx->pos};
}

static inline Span span_at(const Lexer *lx, uint32_t offset) {
    return (Span){lx->file, offset, offset + 1};
}

static inline bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// Value of a digit in any base up to 16, or 99 for any other byte.
static inline int digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 99;
}

Token *emit(Lexer *lx, TokenKind kind, uint32_t start);
// `text` copied to the arena, for a token's `as.text`.
const StrView *token_text(Lexer *lx, StrView text);

// literals.c
void lex_number(Lexer *lx);
void lex_string_part(Lexer *lx, uint32_t start, bool opening);
void lex_byte_literal(Lexer *lx);

#endif
