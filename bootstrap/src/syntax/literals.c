// Numbers, strings and byte literals (spec 1.7).

#include "syntax/lexer_internal.h"

#include <stdlib.h>

static void error(Lexer *lx, const char *code, Span span, const char *message) {
    diag_error(&lx->session->diags, code, span, "%s", message);
}

// ---------------------------------------------------------------------------
// Numbers

static void skip_digits(Lexer *lx) {
    while (!at_end(lx) && (is_digit(lx->src[lx->pos]) || lx->src[lx->pos] == '_')) {
        lx->pos++;
    }
}

// `1.5` and `2e10` are floats; `0..10` is a range and `x.0` a tuple field.
static bool at_fraction_or_exponent(const Lexer *lx) {
    char c = peek(lx, 0);
    if (c == '.') {
        return is_digit(peek(lx, 1));
    }
    if (c == 'e' || c == 'E') {
        char next = peek(lx, 1);
        return is_digit(next) || ((next == '+' || next == '-') && is_digit(peek(lx, 2)));
    }
    return false;
}

static void float_rest(Lexer *lx, uint32_t start) {
    if (peek(lx, 0) == '.') {
        lx->pos++;
        skip_digits(lx);
    }
    if (peek(lx, 0) == 'e' || peek(lx, 0) == 'E') {
        lx->pos++;
        if (peek(lx, 0) == '+' || peek(lx, 0) == '-') {
            lx->pos++;
        }
        skip_digits(lx);
    }
    StrBuf digits = {0};
    for (uint32_t i = start; i < lx->pos; i++) {
        if (lx->src[i] != '_') {
            strbuf_push(&digits, lx->src[i]);
        }
    }
    char *end = NULL;
    double value = strtod(digits.data, &end);
    if (end != digits.data + digits.len) {   // `2.5e`: an exponent without digits
        error(lx, "E0005", span_from(lx, start), "invalid float literal");
        value = 0.0;
    }
    emit(lx, TOK_FLOAT, start)->as.float_value = value;
    strbuf_free(&digits);
}

static int number_base(const Lexer *lx) {
    if (peek(lx, 0) != '0') {
        return 10;
    }
    switch (peek(lx, 1)) {
    case 'x': return 16;
    case 'o': return 8;
    case 'b': return 2;
    default: return 10;
    }
}

// Reads digits of `base` (and `_` separators). Sets *overflow if the value
// does not fit in 64 bits.
static uint64_t scan_digits(Lexer *lx, int base, bool *overflow) {
    uint64_t value = 0;
    while (!at_end(lx)) {
        char c = lx->src[lx->pos];
        int digit = digit_value(c);
        if (c != '_' && digit >= base) {
            break;
        }
        lx->pos++;
        if (c == '_') {
            continue;
        }
        if (value > (UINT64_MAX - (uint64_t)digit) / (uint64_t)base) {
            *overflow = true;
        }
        value = value * (uint64_t)base + (uint64_t)digit;
    }
    return value;
}

void lex_number(Lexer *lx) {
    uint32_t start = lx->pos;
    int base = number_base(lx);
    if (base != 10) {
        lx->pos += 2;
    }
    bool overflow = false;
    uint64_t value = scan_digits(lx, base, &overflow);
    if (base == 10 && at_fraction_or_exponent(lx)) {
        float_rest(lx, start);
        return;
    }
    if (is_letter(peek(lx, 0))) {
        error(lx, "E0001", span_at(lx, lx->pos), "unexpected character after number");
    }
    if (overflow) {
        error(lx, "E0005", span_from(lx, start), "integer literal is too large");
        value = 0;
    }
    emit(lx, TOK_INT, start)->as.int_value = value;
}

// ---------------------------------------------------------------------------
// Escapes

// Reads exactly `count` hex digits; returns -1 if they are missing.
static long hex_digits(Lexer *lx, int count) {
    long value = 0;
    for (int i = 0; i < count; i++) {
        int d = digit_value(peek(lx, 0));
        if (d >= 16) {
            return -1;
        }
        value = value * 16 + d;
        lx->pos++;
    }
    return value;
}

// Reads `{H...}` after `\u`; returns -1 if malformed.
static long unicode_escape(Lexer *lx) {
    if (peek(lx, 0) != '{') {
        return -1;
    }
    lx->pos++;
    long value = 0;
    int digits = 0;
    while (digit_value(peek(lx, 0)) < 16) {
        if (digits == 6) {
            return -1;
        }
        value = value * 16 + digit_value(peek(lx, 0));
        digits++;
        lx->pos++;
    }
    if (digits == 0 || peek(lx, 0) != '}') {
        return -1;
    }
    lx->pos++;
    return value;
}

// Appends the UTF-8 encoding of a Unicode scalar value.
static void push_utf8(StrBuf *buf, uint32_t c) {
    if (c < 0x80) {
        strbuf_push(buf, (char)c);
        return;
    }
    if (c < 0x800) {
        strbuf_push(buf, (char)(0xc0 | (c >> 6)));
    } else if (c < 0x10000) {
        strbuf_push(buf, (char)(0xe0 | (c >> 12)));
        strbuf_push(buf, (char)(0x80 | ((c >> 6) & 0x3f)));
    } else {
        strbuf_push(buf, (char)(0xf0 | (c >> 18)));
        strbuf_push(buf, (char)(0x80 | ((c >> 12) & 0x3f)));
        strbuf_push(buf, (char)(0x80 | ((c >> 6) & 0x3f)));
    }
    strbuf_push(buf, (char)(0x80 | (c & 0x3f)));   // the last continuation byte
}

static void numeric_escape(Lexer *lx, uint32_t start, char kind, StrBuf *out) {
    if (kind == 'x') {
        long value = hex_digits(lx, 2);
        if (value < 0 || value > 0x7f) {
            error(lx, "E0004", span_from(lx, start),
                  "`\\x` escapes in strings must be between \\x00 and \\x7f");
        } else {
            strbuf_push(out, (char)value);
        }
        return;
    }
    long value = unicode_escape(lx);
    if (value < 0 || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        error(lx, "E0004", span_from(lx, start), "invalid unicode escape");
    } else {
        push_utf8(out, (uint32_t)value);
    }
}

static void escape(Lexer *lx, StrBuf *out) {
    uint32_t start = lx->pos;
    lx->pos++;   // the backslash
    char c = peek(lx, 0);
    if (!at_end(lx)) {
        lx->pos++;
    }
    switch (c) {
    case 'n': strbuf_push(out, '\n'); break;
    case 't': strbuf_push(out, '\t'); break;
    case 'r': strbuf_push(out, '\r'); break;
    case '0': strbuf_push(out, '\0'); break;
    case '\\': case '"': case '{': case '}': strbuf_push(out, c); break;
    case 'x': case 'u': numeric_escape(lx, start, c, out); break;
    default: error(lx, "E0004", span_from(lx, start), "invalid escape sequence"); break;
    }
}

// ---------------------------------------------------------------------------
// Strings

static bool is_string_special(char c) {
    return c == '"' || c == '\\' || c == '{' || c == '}' || c == '\n';
}

// Copies a run of ordinary bytes. Runs stop only at ASCII bytes, so each run
// of the (valid UTF-8) source is itself valid UTF-8.
static void copy_run(Lexer *lx, StrBuf *text) {
    uint32_t run = lx->pos;
    while (!at_end(lx) && !is_string_special(lx->src[lx->pos])) {
        lx->pos++;
    }
    strbuf_append(text, (StrView){lx->src + run, lx->pos - run});
}

// Reads string text up to the closing quote or the next interpolation and
// returns the token kind: STR or STR_END after a quote (or an error),
// STR_START or STR_MID before an interpolation.
static TokenKind scan_string_text(Lexer *lx, uint32_t start, bool opening, StrBuf *text) {
    for (;;) {
        char c = peek(lx, 0);
        if (at_end(lx) || c == '\n') {
            error(lx, "E0002", span_from(lx, start), "unterminated string literal");
            return opening ? TOK_STR : TOK_STR_END;
        }
        if (c == '"' || c == '{') {
            lx->pos++;
            if (c == '"') {
                return opening ? TOK_STR : TOK_STR_END;
            }
            VEC_PUSH(lx->interpolations, (uint32_t)lx->brackets.len);
            return opening ? TOK_STR_START : TOK_STR_MID;
        }
        if (c == '}') {
            error(lx, "E0006", span_at(lx, lx->pos),
                  "unescaped `}` in string literal; write `\\}`");
            lx->pos++;
        } else if (c == '\\') {
            escape(lx, text);
        } else {
            copy_run(lx, text);
        }
    }
}

// `opening` is true right after the opening quote and false after the `}`
// that ends an interpolation.
void lex_string_part(Lexer *lx, uint32_t start, bool opening) {
    StrBuf text = {0};
    TokenKind kind = scan_string_text(lx, start, opening, &text);
    StrView view = strbuf_view(&text);
    emit(lx, kind, start)->as.text =
        (StrView){arena_strndup(&lx->session->arena, view.ptr, view.len), view.len};
    strbuf_free(&text);
}

// ---------------------------------------------------------------------------
// Bytes

static long byte_escape(Lexer *lx) {
    char e = peek(lx, 0);
    if (!at_end(lx)) {
        lx->pos++;
    }
    switch (e) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '0': return 0;
    case '\\': return '\\';
    case '\'': return '\'';
    case 'x': return hex_digits(lx, 2);
    default: return -1;
    }
}

// Skips to just after the closing quote on this line, if any.
static void skip_to_quote(Lexer *lx) {
    while (!at_end(lx) && lx->src[lx->pos] != '\'' && lx->src[lx->pos] != '\n') {
        lx->pos++;
    }
    if (peek(lx, 0) == '\'') {
        lx->pos++;
    }
}

// `b'a'`, `b'\n'`, `b'\xff'`: any byte value, unlike `\x` in strings.
void lex_byte_literal(Lexer *lx) {
    uint32_t start = lx->pos;
    lx->pos += 2;   // b'
    long value = (unsigned char)peek(lx, 0);
    if (!at_end(lx)) {
        lx->pos++;
    }
    if (value == '\\') {
        value = byte_escape(lx);
    }
    if (value < 0 || peek(lx, 0) != '\'') {
        error(lx, "E0004", span_from(lx, start), "invalid byte literal");
        skip_to_quote(lx);
        value = 0;
    } else {
        lx->pos++;
    }
    emit(lx, TOK_BYTE, start)->as.byte_value = (uint8_t)value;
}
