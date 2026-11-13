// Patterns (spec appendix A.7).

#include "syntax/parser_internal.h"

static Pattern *new_pattern(Parser *p, PatternKind kind, Span start) {
    Pattern *pat = NEW(p, Pattern);
    pat->kind = kind;
    pat->span = span_since(p, start);
    return pat;
}

// `(p, q)` after the `(`; also used for variant and tuple struct arguments.
static PatternList pattern_list(Parser *p) {
    Vec(Pattern *) items = {0};
    while (!at(p, TOK_RPAREN) && !at(p, TOK_EOF)) {
        VEC_PUSH(items, parse_pattern(p));
        if (!accept(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, TOK_RPAREN, "`)` to close the patterns");
    PatternList list;
    TO_LIST(p, items, list);
    return list;
}

// A literal, optionally followed by `..` or `..=` and a second literal.
static Pattern *literal_or_range(Parser *p, Span start) {
    Expr *lo = parse_literal(p);
    if (!at(p, TOK_DOT_DOT) && !at(p, TOK_DOT_DOT_EQ)) {
        Pattern *pat = new_pattern(p, PAT_LITERAL, start);
        pat->as.literal = lo;
        return pat;
    }
    bool inclusive = advance(p)->kind == TOK_DOT_DOT_EQ;
    Expr *hi = parse_literal(p);
    if (!hi) {
        unexpected(p, "a literal to end the range");
    }
    Pattern *pat = new_pattern(p, PAT_RANGE, start);
    pat->as.range.lo = lo;
    pat->as.range.hi = hi;
    pat->as.range.inclusive = inclusive;
    return pat;
}

// `.Name` or `.Name(p, q)`; the `.` is next.
static Pattern *variant_shorthand(Parser *p, Span start) {
    advance(p);
    const Token *name = expect(p, TOK_IDENT, "a variant name after `.`");
    Pattern *pat = new_pattern(p, PAT_VARIANT, start);
    pat->as.variant.name = name ? name->as.symbol : (Symbol){0};
    if (accept(p, TOK_LPAREN)) {
        pat->as.variant.args = pattern_list(p);
        pat->as.variant.has_args = true;
    }
    pat->span = span_since(p, start);
    return pat;
}

// `Point{x, y: 0, ..}` after the `{`.
static void struct_fields(Parser *p, Pattern *pat) {
    Vec(FieldPattern) fields = {0};
    skip_newlines(p);
    while (at(p, TOK_IDENT)) {
        FieldPattern f = {.name = advance(p)->as.symbol};
        if (accept(p, TOK_COLON)) {
            f.pattern = parse_pattern(p);
        }
        VEC_PUSH(fields, f);
        skip_newlines(p);
        if (!accept(p, TOK_COMMA)) {
            break;
        }
        skip_newlines(p);
    }
    pat->as.struct_.rest = accept(p, TOK_DOT_DOT);
    skip_newlines(p);
    expect(p, TOK_RBRACE, "`}` to close the struct pattern");
    TO_LIST(p, fields, pat->as.struct_.fields);
}

// A pattern starting with a name: a binding, `Enum.Variant(...)`,
// `TupleStruct(...)` or `Struct{...}`.
static Pattern *named(Parser *p, Span start) {
    Vec(Symbol) path = {0};
    VEC_PUSH(path, advance(p)->as.symbol);
    while (at(p, TOK_DOT) && peek_at(p, 1) == TOK_IDENT) {
        advance(p);
        VEC_PUSH(path, advance(p)->as.symbol);
    }
    Pattern *pat;
    if (accept(p, TOK_LBRACE)) {
        pat = new_pattern(p, PAT_STRUCT, start);
        struct_fields(p, pat);
        TO_LIST(p, path, pat->as.struct_.path);
    } else if (path.len == 1 && accept(p, TOK_LPAREN)) {
        pat = new_pattern(p, PAT_TUPLE_STRUCT, start);
        pat->as.tuple_struct.args = pattern_list(p);
        TO_LIST(p, path, pat->as.tuple_struct.path);
    } else if (path.len > 1) {
        pat = new_pattern(p, PAT_VARIANT, start);
        pat->as.variant.name = VEC_POP(path);
        pat->as.variant.has_args = accept(p, TOK_LPAREN);
        if (pat->as.variant.has_args) {
            pat->as.variant.args = pattern_list(p);
        }
        TO_LIST(p, path, pat->as.variant.path);
    } else {
        Symbol name = path.data[0];
        VEC_FREE(path);
        bool wildcard = symbol_eq(name, p->words.wildcard);
        if (p->type_tests && at(p, TOK_COLON)) {
            syntax_error(p, peek_token(p, 0)->span, "type tests are not supported by stage 0");
        }
        pat = new_pattern(p, wildcard ? PAT_WILDCARD : PAT_BINDING, start);
        pat->as.binding.name = name;
    }
    pat->span = span_since(p, start);
    return pat;
}

static Pattern *atom(Parser *p) {
    Span start = peek_token(p, 0)->span;
    switch (peek(p)) {
    case TOK_IDENT: return named(p, start);
    case TOK_DOT: return variant_shorthand(p, start);
    case TOK_KW_MUT: {
        advance(p);
        const Token *name = expect(p, TOK_IDENT, "a name after `mut`");
        Pattern *pat = new_pattern(p, PAT_BINDING, start);
        pat->as.binding.name = name ? name->as.symbol : (Symbol){0};
        pat->as.binding.mutable = true;
        return pat;
    }
    case TOK_LPAREN: {
        advance(p);
        Pattern *pat = new_pattern(p, PAT_TUPLE, start);
        pat->as.tuple = pattern_list(p);
        pat->span = span_since(p, start);
        return pat;
    }
    default:
        if (at_literal(p)) {
            return literal_or_range(p, start);
        }
        unexpected(p, "a pattern");
        return new_pattern(p, PAT_WILDCARD, start);
    }
}

static Pattern *pattern_level(Parser *p);

Pattern *parse_pattern(Parser *p) {
    Pattern *pat = nest(p) ? pattern_level(p)
                           : new_pattern(p, PAT_WILDCARD, peek_token(p, 0)->span);
    p->depth--;
    return pat;
}

static Pattern *pattern_level(Parser *p) {
    Span start = peek_token(p, 0)->span;
    Pattern *first = atom(p);
    if (!at(p, TOK_PIPE)) {
        return first;
    }
    Vec(Pattern *) alternatives = {0};
    VEC_PUSH(alternatives, first);
    while (accept(p, TOK_PIPE)) {
        skip_newlines(p);
        VEC_PUSH(alternatives, atom(p));
    }
    Pattern *pat = new_pattern(p, PAT_OR, start);
    TO_LIST(p, alternatives, pat->as.alternatives);
    return pat;
}

Pattern *parse_test_pattern(Parser *p) {
    bool saved = p->type_tests;
    p->type_tests = true;
    Pattern *pat = parse_pattern(p);
    p->type_tests = saved;
    return pat;
}
