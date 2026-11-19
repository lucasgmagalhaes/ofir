// Lexer: source text → tokens (spec chapter 1).

#ifndef OFIR_LEXER_H
#define OFIR_LEXER_H

#include "session.h"
#include "syntax/token.h"

// Appends the tokens of `file` to `out`, ending with TOK_EOF. Decoded string
// contents are allocated in the session arena. Errors go to the session
// diagnostics; the token list is always complete so parsing can continue.
void lex(Session *session, FileId file, TokenList *out);

typedef Vec(StrView) DocTexts;

// A lexer that runs on demand, so the parser holds only the tokens it is
// working on: each `lex_step` appends the next tokens to `out`.
typedef struct {
    Session *session;
    const char *src;
    uint32_t len;
    uint32_t pos;
    FileId file;
    TokenList *out;
    Vec(char) brackets;            // open `(`, `[` and `{`, innermost last
    Vec(uint32_t) interpolations;  // brackets.len when each interpolation opened
    DocTexts *docs;                // if set, `@module` texts go here and no doc token is made
    bool done;                     // TOK_EOF was appended
} Lexer;

// Checks the encoding of the whole file first (errors go to the diagnostics).
void lexer_init(Lexer *lx, Session *session, FileId file, TokenList *out);
// Appends the tokens of the next `bytes` of source (or a little more, to
// the end of a token), and TOK_EOF at the end; false once that was appended.
bool lex_step(Lexer *lx, uint32_t bytes);
void lexer_free(Lexer *lx);

// Interns every keyword. Must run on a fresh interner, before any other
// name; session_init does it.
void intern_keywords(Interner *names);

#endif
