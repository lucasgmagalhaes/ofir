// Definitions shared by the files of the parser.

#ifndef OFIR_PARSER_INTERNAL_H
#define OFIR_PARSER_INTERNAL_H

#include "base/vec.h"
#include "syntax/lexer.h"
#include "syntax/parser.h"

#include <string.h>

// The tokens are lexed as the parser asks for them, into chunks that are
// freed once the parser has moved past them (at top-level item boundaries,
// where it holds no token pointer). A token pointer stays valid until then.
#define TOKEN_CHUNK_BITS 10
#define TOKEN_CHUNK ((size_t)1 << TOKEN_CHUNK_BITS)
#define LEX_BYTES 4096        // source bytes lexed at a time

typedef struct {
    Lexer lexer;
    TokenList staged;         // the lexer's output, before it moves to a chunk
    Vec(Token *) chunks;      // chunk k holds tokens k * TOKEN_CHUNK onwards
    size_t freed;             // chunks before this one are freed
    size_t count;             // tokens so far, the last one TOK_EOF once lexed
    size_t ready;             // tokens `token_index` reads directly: count, or 0 once failed
    DocTexts docs;            // `//!` comments; `///` ones are dropped (spec 12.5)
    bool failed;              // the lexer reported an error: the parse stops there
} TokenStream;

typedef struct {
    const char *code;
    Span span;
    char *message;
} SyntaxError;

typedef struct {
    Session *session;
    FileId file;
    TokenStream *tokens;
    size_t pos;
    // Reported once the file is parsed, and only if it lexed without errors
    // (as if it had been lexed whole first).
    Vec(SyntaxError) errors;
    bool no_struct_literal;   // in `if`, `match` and `for` headers (spec A.6)
    bool recovering;          // an error was reported; silent until resynchronized
    bool type_tests;          // in a `match` arm or `if const` pattern: `name: T` (spec 9.8)
    bool too_deep;            // past MAX_NESTING (E0101): the parse is unwinding to the end
    int depth;                // open levels of nesting (`nest`)
    struct {                  // contextual keywords (spec 1.5)
        Symbol move, type, ref, where, wildcard;
    } words;
} Parser;

// ---- tokens (parser.c)
const Token *token_pull(const Parser *p, size_t index);   // lexes up to `index`

// The token at `index`, clamped to the EOF token.
static inline const Token *token_index(const Parser *p, size_t index) {
    const TokenStream *ts = p->tokens;
    if (index < ts->ready) {
        return &ts->chunks.data[index >> TOKEN_CHUNK_BITS][index & (TOKEN_CHUNK - 1)];
    }
    return token_pull(p, index);
}
static inline const Token *peek_token(const Parser *p, size_t ahead) {
    return token_index(p, p->pos + ahead);
}

static inline TokenKind peek(const Parser *p) {
    return peek_token(p, 0)->kind;
}

static inline bool at(const Parser *p, TokenKind kind) {
    return peek(p) == kind;
}

TokenKind peek_at(const Parser *p, size_t ahead);
const Token *advance(Parser *p);
bool accept(Parser *p, TokenKind kind);
const Token *expect(Parser *p, TokenKind kind, const char *what);
bool at_word(const Parser *p, Symbol word);   // an identifier with this name
void skip_newlines(Parser *p);
// A `,`, a newline, or both, between list members; false if neither.
bool accept_separator(Parser *p);
void expect_terminator(Parser *p);            // newline, or before `}` / end of file
void synchronize(Parser *p);                  // skip to the next statement boundary
Span span_since(const Parser *p, Span start); // from `start` to the previous token
void syntax_error(Parser *p, Span span, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
void unexpected(Parser *p, const char *what);  // "expected <what>, found <token>"
void *parser_alloc(Parser *p, size_t size);

// Nesting limit (E0101): each level of expressions, types and patterns, and
// each step of an operator or postfix chain, is one level. `nest` enters a
// level; past the limit it reports E0101 once, moves to the end of the file
// and returns false. The caller leaves the level (`depth--`) either way.
#define MAX_NESTING 256
bool nest(Parser *p);

#define NEW(p, T) ((T *)parser_alloc((p), sizeof(T)))

// Copies a Vec into an arena List and frees the Vec.
#define TO_LIST(p, vec, list)                                                    \
    do {                                                                         \
        (list).len = (uint32_t)(vec).len;                                        \
        (list).items = parser_alloc((p), sizeof(*(vec).data) * (vec).len);       \
        if ((vec).len) memcpy((list).items, (vec).data, sizeof(*(vec).data) * (vec).len); \
        VEC_FREE(vec);                                                           \
    } while (0)

// ---- types (parse_types.c)
TypeExpr *parse_type(Parser *p);
TypeExpr *parse_simple_type(Parser *p);   // without `[`: the target of `as`
bool can_start_type(const Parser *p);
TypeList parse_type_args(Parser *p);                // after `<`, up to and including `>`
typedef Vec(WherePred) WhereVec;
SymbolList parse_type_params(Parser *p, WhereVec *preds);  // optional `<T, U: Eq>`
TypeList parse_bounds(Parser *p);                   // `Eq + Hash`
Mode parse_mode(Parser *p);                         // optional `mut` / `move`

// ---- patterns (parse_patterns.c)
Pattern *parse_pattern(Parser *p);
Pattern *parse_test_pattern(Parser *p);   // where stage 1 reads `name: T` as a type test

// ---- expressions (parse_exprs.c, parse_primary.c)
Expr *parse_expr(Parser *p);
Expr *parse_expr_no_struct(Parser *p);     // for `if`, `match` and `for` headers
bool at_literal(const Parser *p);          // INT, FLOAT, STR, BYTE, true, false, `-` number
Expr *parse_literal(Parser *p);            // a literal, or NULL (nothing consumed)
Expr *parse_primary(Parser *p);
Expr *parse_postfix(Parser *p, Expr *base);
Block *parse_block(Parser *p);
Expr *parse_control(Parser *p);            // `{`, `if`, `match` or `unsafe` is next
ArgList parse_call_args(Parser *p);        // after `(`, up to and including `)`
Expr *new_expr(Parser *p, ExprKind kind, Span span);

// ---- statements and items (parse_stmts.c, parse_items.c, parse_decls.c)
Stmt *parse_stmt(Parser *p);

typedef struct {
    Span span;
    AttributeList attributes;
    bool is_pub;
} ItemHeader;

ItemHeader parse_item_header(Parser *p);                     // attributes and `pub`
FnDecl *parse_fn(Parser *p, ItemHeader header, bool with_body);  // after `fn`
void parse_struct(Parser *p, Item *item);                     // after `struct`
void parse_enum(Parser *p, Item *item);                       // after `enum`

#endif
