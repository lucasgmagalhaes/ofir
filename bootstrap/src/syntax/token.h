// Tokens (spec chapter 1).

#ifndef OFIR_TOKEN_H
#define OFIR_TOKEN_H

#include "base/intern.h"
#include "base/source.h"
#include "base/vec.h"

#include <stdio.h>

// X(name, text): `text` is used by `--emit=tokens` and in error messages.
#define TOKEN_KINDS(X)                                                        \
    /* literals, names and layout */                                          \
    X(IDENT, "ident") X(INT, "int") X(FLOAT, "float") X(BYTE, "byte")         \
    X(STR, "str") X(STR_START, "str_start") X(STR_MID, "str_mid")             \
    X(STR_END, "str_end") X(DOC_COMMENT, "doc") X(MODULE_DOC, "module_doc")   \
    X(NEWLINE, "newline") X(INVALID, "invalid") X(EOF, "eof")                 \
    /* reserved keywords */                                                   \
    X(KW_AS, "as") X(KW_BREAK, "break") X(KW_CONST, "const")                  \
    X(KW_CONTINUE, "continue") X(KW_COPY, "copy") X(KW_DYN, "dyn")            \
    X(KW_ELSE, "else") X(KW_ENUM, "enum") X(KW_EXTERN, "extern")              \
    X(KW_FALSE, "false") X(KW_FN, "fn") X(KW_FOR, "for") X(KW_IF, "if")       \
    X(KW_IMPORT, "import") X(KW_IN, "in") X(KW_INTERFACE, "interface")        \
    X(KW_LET, "let") X(KW_MATCH, "match") X(KW_MUT, "mut") X(KW_PUB, "pub")   \
    X(KW_RETURN, "return") X(KW_SELF_VALUE, "self") X(KW_SELF_TYPE, "Self")   \
    X(KW_STRUCT, "struct") X(KW_TRUE, "true") X(KW_UNSAFE, "unsafe")          \
    /* punctuation; `>` is never combined (spec 1.6) */                       \
    X(LPAREN, "(") X(RPAREN, ")") X(LBRACE, "{") X(RBRACE, "}")               \
    X(LBRACKET, "[") X(RBRACKET, "]") X(COMMA, ",") X(COLON, ":")             \
    X(SEMICOLON, ";") X(DOT, ".") X(DOT_DOT, "..") X(DOT_DOT_EQ, "..=")       \
    X(FAT_ARROW, "=>") X(QUESTION, "?") X(QUESTION_QUESTION, "??") X(AT, "@") \
    X(PLUS, "+") X(MINUS, "-") X(STAR, "*") X(SLASH, "/") X(PERCENT, "%")     \
    X(AMP, "&") X(PIPE, "|") X(CARET, "^") X(SHL, "<<") X(BANG, "!")          \
    X(EQ, "=") X(EQ_EQ, "==") X(NE, "!=") X(LT, "<") X(LE, "<=") X(GT, ">")   \
    X(AND_AND, "&&") X(OR_OR, "||")                                           \
    X(PLUS_EQ, "+=") X(MINUS_EQ, "-=") X(STAR_EQ, "*=") X(SLASH_EQ, "/=")     \
    X(PERCENT_EQ, "%=") X(AMP_EQ, "&=") X(PIPE_EQ, "|=") X(CARET_EQ, "^=")    \
    X(SHL_EQ, "<<=")

typedef enum {
#define X(name, text) TOK_##name,
    TOKEN_KINDS(X)
#undef X
    TOK_COUNT
} TokenKind;

typedef struct {
    TokenKind kind;
    Span span;
    union {
        uint64_t int_value;     // TOK_INT
        double float_value;     // TOK_FLOAT
        uint8_t byte_value;     // TOK_BYTE
        Symbol symbol;          // TOK_IDENT
        // strings (escapes decoded) and doc comments; a pointer keeps a token at
        // 24 bytes (most tokens carry no text)
        const StrView *text;
    } as;
} Token;

typedef Vec(Token) TokenList;

const char *token_kind_text(TokenKind kind);

// Writes one token per line, as used by `--emit=tokens` and tests/lex.
void print_tokens(const TokenList *tokens, const SourceMap *map, const Interner *names, FILE *out);

#endif
