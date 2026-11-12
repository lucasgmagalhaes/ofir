// Lexer main loop, comments, words and punctuation. Literals are in
// literals.c. Port of the stage 1 prototype (compiler/syntax/lexer.of).

#include "syntax/lexer_internal.h"

#include <string.h>

// Keywords are interned first in every session (see intern_keywords), so
// the symbol of keyword `i` has id `i + 1` and a word needs a single hash
// lookup to be classified.
static const struct {
    StrView word;
    TokenKind kind;
} keywords[] = {
    {SV_INIT("as"), TOK_KW_AS}, {SV_INIT("break"), TOK_KW_BREAK},
    {SV_INIT("const"), TOK_KW_CONST}, {SV_INIT("continue"), TOK_KW_CONTINUE},
    {SV_INIT("copy"), TOK_KW_COPY}, {SV_INIT("dyn"), TOK_KW_DYN}, {SV_INIT("else"), TOK_KW_ELSE},
    {SV_INIT("enum"), TOK_KW_ENUM}, {SV_INIT("extern"), TOK_KW_EXTERN},
    {SV_INIT("false"), TOK_KW_FALSE}, {SV_INIT("fn"), TOK_KW_FN}, {SV_INIT("for"), TOK_KW_FOR},
    {SV_INIT("if"), TOK_KW_IF}, {SV_INIT("import"), TOK_KW_IMPORT}, {SV_INIT("in"), TOK_KW_IN},
    {SV_INIT("interface"), TOK_KW_INTERFACE}, {SV_INIT("let"), TOK_KW_LET},
    {SV_INIT("match"), TOK_KW_MATCH}, {SV_INIT("mut"), TOK_KW_MUT}, {SV_INIT("pub"), TOK_KW_PUB},
    {SV_INIT("return"), TOK_KW_RETURN}, {SV_INIT("self"), TOK_KW_SELF_VALUE},
    {SV_INIT("Self"), TOK_KW_SELF_TYPE}, {SV_INIT("struct"), TOK_KW_STRUCT},
    {SV_INIT("true"), TOK_KW_TRUE}, {SV_INIT("unsafe"), TOK_KW_UNSAFE},
};

void intern_keywords(Interner *names) {
    for (size_t i = 0; i < ARRAY_LEN(keywords); i++) {
        Symbol symbol = intern(names, keywords[i].word);
        if (symbol.id != i + 1) {
            fatal("keywords must be interned before any other name");
        }
    }
}

Token *emit(Lexer *lx, TokenKind kind, uint32_t start) {
    Token token = {.kind = kind, .span = span_from(lx, start)};
    VEC_PUSH(*lx->out, token);
    return &VEC_LAST(*lx->out);
}

// Newlines are tokens except directly inside `(...)` and `[...]` (spec 1.3).
static bool newlines_matter(const Lexer *lx) {
    return lx->brackets.len == 0 || VEC_LAST(lx->brackets) == '{';
}

// ---------------------------------------------------------------------------
// Comments

static StrView trimmed_copy(Lexer *lx, uint32_t start, uint32_t end) {
    while (start < end && (lx->src[start] == ' ' || lx->src[start] == '\t')) {
        start++;
    }
    while (end > start && (lx->src[end - 1] == ' ' || lx->src[end - 1] == '\r')) {
        end--;
    }
    return (StrView){arena_strndup(&lx->session->arena, lx->src + start, end - start), end - start};
}

// Skips a `//` comment.
static void line_comment(Lexer *lx) {
    while (!at_end(lx) && lx->src[lx->pos] != '\n') {
        lx->pos++;
    }
}

// The text of a documentation comment (spec 1.8): each line without its
// indentation and leading `*`, blank lines at both ends removed.
static StrView doc_text(Lexer *lx, uint32_t start, uint32_t end) {
    StrBuf text = {0};
    uint32_t pos = start;
    while (pos < end) {
        uint32_t line_end = pos;
        while (line_end < end && lx->src[line_end] != '\n') {
            line_end++;
        }
        uint32_t from = pos;
        while (from < line_end && (lx->src[from] == ' ' || lx->src[from] == '\t')) {
            from++;
        }
        if (from < line_end && lx->src[from] == '*') {
            from += from + 1 < line_end && lx->src[from + 1] == ' ' ? 2 : 1;
        }
        StrView line = trimmed_copy(lx, from, line_end);
        if (line.len > 0 || text.len > 0) {   // no blank lines at the start
            strbuf_append(&text, line);
            strbuf_push(&text, '\n');
        }
        pos = line_end + 1;
    }
    while (text.len > 0 && text.data[text.len - 1] == '\n') {   // nor at the end
        text.data[--text.len] = '\0';
    }
    StrView out = {arena_strndup(&lx->session->arena, text.data ? text.data : "", text.len),
                   text.len};
    strbuf_free(&text);
    return out;
}

// `/** ... */` documents the next declaration, or the file when its text
// starts with `@module` (spec 1.8).
static void doc_comment(Lexer *lx, uint32_t start) {
    StrView text = doc_text(lx, start + 3, lx->pos - 2);
    bool module = text.len >= 7 && memcmp(text.ptr, "@module", 7) == 0;
    while (module && text.len > 0 && (text.ptr[0] == '@' || text.ptr[0] > ' ')) {
        text.ptr++;   // the tag itself is not part of the text
        text.len--;
    }
    while (module && text.len > 0 && (text.ptr[0] == '\n' || text.ptr[0] == ' ')) {
        text.ptr++;
        text.len--;
    }
    Token *t = emit(lx, module ? TOK_MODULE_DOC : TOK_DOC_COMMENT, start);
    t->as.text = text;
}

// Skips `/* ... */`; `/** ... */` is a documentation comment. Returns true
// if the comment contained a newline.
static bool block_comment(Lexer *lx) {
    uint32_t start = lx->pos;
    bool doc = peek(lx, 2) == '*' && peek(lx, 3) != '/' && peek(lx, 3) != '*';
    lx->pos += 2;
    bool newline = false;
    for (;;) {
        if (at_end(lx)) {
            diag_error(&lx->session->diags, "E0003", span_from(lx, start),
                       "unterminated block comment");
            return newline;
        }
        if (lx->src[lx->pos] == '*' && peek(lx, 1) == '/') {
            lx->pos += 2;
            if (doc) {
                doc_comment(lx, start);
            }
            return newline;
        }
        newline |= lx->src[lx->pos] == '\n';
        lx->pos++;
    }
}

// ---------------------------------------------------------------------------
// Words

// Stage 1 lexes byte and raw strings: `b"` or `r"` reports where one starts.
static void stage1_string(Lexer *lx, uint32_t start, char prefix) {
    if (prefix == 'b' && lx->src[lx->pos] == '#') {
        return;
    }
    lx->pos++;
    diag_error(&lx->session->diags, "E0100", span_from(lx, start),
               "%s strings are not supported by stage 0", prefix == 'b' ? "byte" : "raw");
    lx->pos--;
}

static void word(Lexer *lx) {
    uint32_t start = lx->pos;
    while (!at_end(lx) && (is_letter(lx->src[lx->pos]) || is_digit(lx->src[lx->pos]))) {
        lx->pos++;
    }
    char c = lx->src[start], next = lx->src[lx->pos];
    if (lx->pos - start == 1 && (c == 'b' || c == 'r') && (next == '"' || next == '#')) {
        stage1_string(lx, start, c);
    }
    Symbol symbol = intern(&lx->session->names, (StrView){lx->src + start, lx->pos - start});
    if (symbol.id <= ARRAY_LEN(keywords)) {
        emit(lx, keywords[symbol.id - 1].kind, start);
        return;
    }
    emit(lx, TOK_IDENT, start)->as.symbol = symbol;
}

// ---------------------------------------------------------------------------
// Punctuation

// Consumes `expected` if it is the next byte.
static bool eat(Lexer *lx, char expected) {
    if (peek(lx, 0) == expected) {
        lx->pos++;
        return true;
    }
    return false;
}

static TokenKind pick(Lexer *lx, char expected, TokenKind if_match, TokenKind otherwise) {
    return eat(lx, expected) ? if_match : otherwise;
}

static TokenKind open_bracket(Lexer *lx, char bracket, TokenKind kind) {
    VEC_PUSH(lx->brackets, bracket);
    return kind;
}

// Mismatched brackets are reported by the parser; the lexer only keeps its
// stack balanced.
static TokenKind close_bracket(Lexer *lx, TokenKind kind) {
    if (lx->brackets.len) {
        lx->brackets.len--;
    }
    return kind;
}

static bool closes_interpolation(Lexer *lx) {
    if (lx->interpolations.len > 0 && VEC_LAST(lx->interpolations) == lx->brackets.len) {
        lx->interpolations.len--;
        return true;
    }
    return false;
}

static TokenKind invalid_character(Lexer *lx, uint32_t start) {
    // Skip the rest of a multi-byte UTF-8 character so it is reported once.
    while (!at_end(lx) && ((unsigned char)lx->src[lx->pos] & 0xc0) == 0x80) {
        lx->pos++;
    }
    diag_error(&lx->session->diags, "E0001", span_from(lx, start), "unexpected character");
    return TOK_INVALID;
}

// `>` is always a single token: the parser joins adjacent ones (spec 1.6).
static TokenKind punct_kind(Lexer *lx, char c, uint32_t start) {
    switch (c) {
    case '(': return open_bracket(lx, '(', TOK_LPAREN);
    case '[': return open_bracket(lx, '[', TOK_LBRACKET);
    case '{': return open_bracket(lx, '{', TOK_LBRACE);
    case ')': return close_bracket(lx, TOK_RPAREN);
    case ']': return close_bracket(lx, TOK_RBRACKET);
    case '}': return close_bracket(lx, TOK_RBRACE);
    case ',': return TOK_COMMA;
    case ':': return TOK_COLON;
    case ';': return TOK_SEMICOLON;
    case '@': return TOK_AT;
    case '>': return TOK_GT;
    case '?': return pick(lx, '?', TOK_QUESTION_QUESTION, TOK_QUESTION);
    case '.': return eat(lx, '.') ? pick(lx, '=', TOK_DOT_DOT_EQ, TOK_DOT_DOT) : TOK_DOT;
    case '=': return eat(lx, '>') ? TOK_FAT_ARROW : pick(lx, '=', TOK_EQ_EQ, TOK_EQ);
    case '+': return pick(lx, '=', TOK_PLUS_EQ, TOK_PLUS);
    case '-': return pick(lx, '=', TOK_MINUS_EQ, TOK_MINUS);
    case '*': return pick(lx, '=', TOK_STAR_EQ, TOK_STAR);
    case '/': return pick(lx, '=', TOK_SLASH_EQ, TOK_SLASH);
    case '%': return pick(lx, '=', TOK_PERCENT_EQ, TOK_PERCENT);
    case '^': return pick(lx, '=', TOK_CARET_EQ, TOK_CARET);
    case '!': return pick(lx, '=', TOK_NE, TOK_BANG);
    case '&': return eat(lx, '&') ? TOK_AND_AND : pick(lx, '=', TOK_AMP_EQ, TOK_AMP);
    case '|': return eat(lx, '|') ? TOK_OR_OR : pick(lx, '=', TOK_PIPE_EQ, TOK_PIPE);
    case '<':
        return eat(lx, '<') ? pick(lx, '=', TOK_SHL_EQ, TOK_SHL) : pick(lx, '=', TOK_LE, TOK_LT);
    default: return invalid_character(lx, start);
    }
}

static void punct(Lexer *lx) {
    uint32_t start = lx->pos;
    char c = lx->src[lx->pos++];
    if (c == '}' && closes_interpolation(lx)) {
        lex_string_part(lx, start, false);
        return;
    }
    emit(lx, punct_kind(lx, c, start), start);
}

// ---------------------------------------------------------------------------
// Main loop

static void token_at(Lexer *lx) {
    uint32_t start = lx->pos;
    char c = lx->src[start];
    if (c == 'b' && peek(lx, 1) == '\'') {
        lex_byte_literal(lx);
    } else if (is_letter(c)) {
        word(lx);
    } else if (is_digit(c)) {
        lex_number(lx);
    } else if (c == '"') {
        lx->pos++;
        lex_string_part(lx, start, true);
    } else {
        punct(lx);
    }
}

// Handles whitespace and comments at the current position. Returns false
// if the current byte starts a token.
static bool skip_trivia(Lexer *lx) {
    char c = lx->src[lx->pos];
    uint32_t start = lx->pos;
    if (c == ' ' || c == '\t' || c == '\r') {
        lx->pos++;
    } else if (c == '\n') {
        lx->pos++;
        if (newlines_matter(lx)) {
            emit(lx, TOK_NEWLINE, start);
        }
    } else if (c == '/' && peek(lx, 1) == '/') {
        line_comment(lx);
    } else if (c == '/' && peek(lx, 1) == '*') {
        if (block_comment(lx) && newlines_matter(lx)) {
            emit(lx, TOK_NEWLINE, start);
        }
    } else {
        return false;
    }
    return true;
}

// Length of the UTF-8 character at `i`, or 0 if the bytes there are not
// one: overlong forms, surrogates and values above U+10FFFF are invalid.
static uint32_t utf8_length(const unsigned char *s, uint32_t i, uint32_t len) {
    unsigned char c = s[i];
    unsigned char lo = 0x80;
    unsigned char hi = 0xbf;
    uint32_t n = 0;
    if (c >= 0xc2 && c <= 0xdf) {
        n = 2;
    } else if (c >= 0xe0 && c <= 0xef) {
        n = 3;
        lo = c == 0xe0 ? 0xa0 : lo;
        hi = c == 0xed ? 0x9f : hi;
    } else if (c >= 0xf0 && c <= 0xf4) {
        n = 4;
        lo = c == 0xf0 ? 0x90 : lo;
        hi = c == 0xf4 ? 0x8f : hi;
    }
    if (n == 0 || n > len - i || s[i + 1] < lo || s[i + 1] > hi) {
        return 0;
    }
    for (uint32_t k = 2; k < n; k++) {
        if ((s[i + k] & 0xc0) != 0x80) {
            return 0;
        }
    }
    return n;
}

// U+202A to U+202E and U+2066 to U+2069 reorder how text is displayed.
static bool is_bidi_control(const unsigned char *s, uint32_t i) {
    return s[i] == 0xe2 && ((s[i + 1] == 0x80 && s[i + 2] >= 0xaa && s[i + 2] <= 0xae) ||
                            (s[i + 1] == 0x81 && s[i + 2] >= 0xa6 && s[i + 2] <= 0xa9));
}

// The end of the ASCII run at `i`, read 32 bytes at a time: most source is
// ASCII, and the check must not slow the lexer down.
static uint32_t skip_ascii(const unsigned char *s, uint32_t i, uint32_t len) {
    const uint64_t high = 0x8080808080808080ull;
    while (len - i >= 32) {
        uint64_t w[4];
        memcpy(w, s + i, sizeof w);
        if (((w[0] | w[1] | w[2] | w[3]) & high) != 0) {
            break;
        }
        i += 32;
    }
    while (i < len && s[i] < 0x80) {
        i++;
    }
    return i;
}

// Source must be UTF-8 without bidirectional controls (spec 1.1).
static void check_encoding(Lexer *lx) {
    const unsigned char *s = (const unsigned char *)lx->src;
    uint32_t i = 0;
    while (i < lx->len) {
        if (s[i] < 0x80) {
            i = skip_ascii(s, i, lx->len);
            continue;
        }
        uint32_t n = utf8_length(s, i, lx->len);
        if (n == 0) {
            uint32_t start = i++;
            while (i < lx->len && (s[i] & 0xc0) == 0x80) {
                i++;
            }
            diag_error(&lx->session->diags, "E0008", (Span){lx->file, start, i}, "invalid UTF-8");
            continue;
        }
        if (n == 3 && is_bidi_control(s, i)) {
            diag_error(&lx->session->diags, "E0009", (Span){lx->file, i, i + 3},
                       "bidirectional control character");
        }
        i += n;
    }
}

void lex(Session *session, FileId file, TokenList *out) {
    const SourceFile *source = source_file(&session->sources, file);
    Lexer lx = {
        .session = session,
        .src = source->text.ptr,
        .len = (uint32_t)source->text.len,
        .file = file,
        .out = out,
    };
    check_encoding(&lx);
    while (!at_end(&lx)) {
        if (!skip_trivia(&lx)) {
            token_at(&lx);
        }
    }
    emit(&lx, TOK_EOF, lx.pos);
    VEC_FREE(lx.brackets);
    VEC_FREE(lx.interpolations);
}
