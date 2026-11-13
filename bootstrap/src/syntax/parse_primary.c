// Primary and postfix expressions: literals, names, aggregates, calls,
// indexing and field access. Control flow expressions are in parse_control.c.

#include "syntax/parser_internal.h"

#include <stdlib.h>

// ---------------------------------------------------------------------------
// Literals

bool at_literal(const Parser *p) {
    switch (peek(p)) {
    case TOK_INT: case TOK_FLOAT: case TOK_BYTE: case TOK_STR: case TOK_KW_TRUE: case TOK_KW_FALSE:
        return true;
    case TOK_MINUS:
        return peek_at(p, 1) == TOK_INT || peek_at(p, 1) == TOK_FLOAT;
    default:
        return false;
    }
}

static Expr *literal_token(Parser *p) {
    const Token *t = advance(p);
    Expr *e = new_expr(p, EXPR_INT, t->span);
    switch (t->kind) {
    case TOK_INT: e->as.int_value = t->as.int_value; break;
    case TOK_FLOAT: e->kind = EXPR_FLOAT; e->as.float_value = t->as.float_value; break;
    case TOK_BYTE: e->kind = EXPR_BYTE; e->as.byte_value = t->as.byte_value; break;
    case TOK_STR: e->kind = EXPR_STR; e->as.str = t->as.text; break;
    default: e->kind = EXPR_BOOL; e->as.bool_value = t->kind == TOK_KW_TRUE; break;
    }
    return e;
}

Expr *parse_literal(Parser *p) {
    if (!at_literal(p)) {
        return NULL;
    }
    if (at(p, TOK_MINUS)) {
        Span start = advance(p)->span;
        Expr *e = new_expr(p, EXPR_UNARY, start);
        e->as.unary.op = OP_NEG;
        e->as.unary.operand = literal_token(p);
        e->span = span_since(p, start);
        return e;
    }
    return literal_token(p);
}

// "a {x} b": STR_START, tokens of x, STR_MID or STR_END, ...
static Expr *interpolation(Parser *p) {
    Span start = peek_token(p, 0)->span;
    Vec(Expr *) parts = {0};
    for (;;) {
        const Token *text = advance(p);
        if (text->as.text.len > 0) {   // "{x}" has empty text around `x`
            Expr *piece = new_expr(p, EXPR_STR, text->span);
            piece->as.str = text->as.text;
            VEC_PUSH(parts, piece);
        }
        if (text->kind == TOK_STR_END || text->kind == TOK_EOF) {
            break;
        }
        bool saved = p->no_struct_literal;
        p->no_struct_literal = false;
        VEC_PUSH(parts, parse_expr(p));
        p->no_struct_literal = saved;
        if (!at(p, TOK_STR_MID) && !at(p, TOK_STR_END)) {
            unexpected(p, "`}` to close the interpolation");
            break;
        }
    }
    Expr *e = new_expr(p, EXPR_INTERP, start);
    TO_LIST(p, parts, e->as.parts);
    return e;
}

// ---------------------------------------------------------------------------
// Names

// Whether `<` at `ahead` starts type arguments: the tokens up to the
// matching `>` look like types and the `>` is followed by `(`, `.` or `{`
// (spec 8.5). Pure lookahead: no tokens are consumed, no errors reported.
static bool looks_like_type_args(const Parser *p, size_t ahead) {
    int depth = 0;
    for (size_t i = ahead; i < ahead + 64; i++) {
        switch (peek_at(p, i)) {
        case TOK_LT: depth++; break;
        case TOK_GT:
            if (--depth == 0) {
                TokenKind next = peek_at(p, i + 1);
                return next == TOK_LPAREN || next == TOK_DOT || next == TOK_LBRACE;
            }
            break;
        case TOK_IDENT: case TOK_COMMA: case TOK_AMP: case TOK_KW_MUT: case TOK_LBRACKET:
        case TOK_RBRACKET: case TOK_LPAREN: case TOK_RPAREN: case TOK_QUESTION: case TOK_STAR:
        case TOK_SEMICOLON: case TOK_INT: case TOK_KW_FN: case TOK_KW_DYN: case TOK_KW_SELF_TYPE:
        case TOK_BANG: case TOK_DOT:
            break;
        default:
            return false;
        }
    }
    return false;
}

static Expr *name(Parser *p) {
    const Token *t = advance(p);
    if (at(p, TOK_LT) && looks_like_type_args(p, 0)) {
        advance(p);
        TypeList args = parse_type_args(p);
        Expr *e = new_expr(p, EXPR_GENERIC, t->span);
        e->as.generic.name = t->as.symbol;
        e->as.generic.args = args;
        return e;
    }
    Expr *e = new_expr(p, EXPR_IDENT, t->span);
    e->as.ident.name = t->as.symbol;
    return e;
}

// `.Name` or `.Name(args)`; the `.` is next.
static Expr *variant_shorthand(Parser *p) {
    Span start = advance(p)->span;
    const Token *t = expect(p, TOK_IDENT, "a variant name after `.`");
    Expr *e = new_expr(p, EXPR_VARIANT, start);
    e->as.variant.name = t ? t->as.symbol : (Symbol){0};
    if (accept(p, TOK_LPAREN)) {
        e->as.variant.args = parse_call_args(p);
        e->as.variant.has_args = true;
    }
    e->span = span_since(p, start);
    return e;
}

// ---------------------------------------------------------------------------
// Aggregates

// `()`, `(e)`, `(e,)` and `(a, b)`; the `(` is next.
static Expr *parenthesized(Parser *p) {
    Span start = advance(p)->span;
    bool saved = p->no_struct_literal;
    p->no_struct_literal = false;
    Vec(Expr *) elems = {0};
    bool tuple = at(p, TOK_RPAREN);
    while (!at(p, TOK_RPAREN) && !at(p, TOK_EOF)) {
        VEC_PUSH(elems, parse_expr(p));
        if (!accept(p, TOK_COMMA)) {
            break;
        }
        tuple = true;
    }
    expect(p, TOK_RPAREN, "`)`");
    p->no_struct_literal = saved;
    if (!tuple && elems.len == 1) {
        Expr *inner = elems.data[0];
        VEC_FREE(elems);
        return inner;
    }
    Expr *e = new_expr(p, EXPR_TUPLE, start);
    TO_LIST(p, elems, e->as.elems);
    return e;
}

// `[a, b]` and `[value; count]`; the `[` is next.
static Expr *array(Parser *p) {
    Span start = advance(p)->span;
    bool saved = p->no_struct_literal;
    p->no_struct_literal = false;
    Vec(Expr *) elems = {0};
    Expr *e = NULL;
    while (!at(p, TOK_RBRACKET) && !at(p, TOK_EOF)) {
        VEC_PUSH(elems, parse_expr(p));
        if (elems.len == 1 && accept(p, TOK_SEMICOLON)) {
            e = new_expr(p, EXPR_ARRAY_REPEAT, start);
            e->as.repeat.value = elems.data[0];
            e->as.repeat.count = parse_expr(p);
            break;
        }
        if (!accept(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, TOK_RBRACKET, "`]` to close the array");
    p->no_struct_literal = saved;
    if (e) {
        VEC_FREE(elems);
    } else {
        e = new_expr(p, EXPR_ARRAY, start);
        TO_LIST(p, elems, e->as.elems);
    }
    e->span = span_since(p, start);
    return e;
}

// A struct literal follows a type name: an identifier starting with an
// uppercase letter, possibly qualified or with type arguments.
static bool is_type_name(const Parser *p, const Expr *e) {
    switch (e->kind) {
    case EXPR_IDENT: return symbol_name(&p->session->names, e->as.ident.name).ptr[0] <= 'Z';
    case EXPR_GENERIC: return true;
    case EXPR_FIELD: return symbol_name(&p->session->names, e->as.field.name).ptr[0] <= 'Z';
    case EXPR_SELF_TYPE: return true;
    default: return false;
    }
}

static void field_inits(Parser *p, Expr *e) {
    Vec(FieldInit) fields = {0};
    skip_newlines(p);
    while (at(p, TOK_IDENT)) {
        FieldInit f = {.name = advance(p)->as.symbol};
        if (accept(p, TOK_COLON)) {
            skip_newlines(p);
            f.value = parse_expr(p);
        }
        VEC_PUSH(fields, f);
        bool separated = accept_separator(p);
        skip_newlines(p);
        if (!separated) {
            break;
        }
    }
    if (accept(p, TOK_DOT_DOT)) {
        e->as.struct_.base = parse_expr(p);
        accept(p, TOK_COMMA);
        skip_newlines(p);
    }
    expect(p, TOK_RBRACE, "`}` to close the struct literal");
    TO_LIST(p, fields, e->as.struct_.fields);
}

static Expr *struct_literal(Parser *p, Expr *type) {
    advance(p);   // {
    bool saved = p->no_struct_literal;
    p->no_struct_literal = false;
    Expr *e = new_expr(p, EXPR_STRUCT, type->span);
    e->as.struct_.type = type;
    field_inits(p, e);
    p->no_struct_literal = saved;
    e->span = span_since(p, type->span);
    return e;
}

// ---------------------------------------------------------------------------
// Closures: stage 0 recognizes them (the same lookahead as stage 1, spec
// 10.1) only to report that it does not support them.

static bool type_start(TokenKind kind) {
    switch (kind) {
    case TOK_IDENT: case TOK_KW_SELF_TYPE: case TOK_AMP: case TOK_STAR: case TOK_LBRACKET:
    case TOK_LPAREN: case TOK_KW_FN: case TOK_BANG: case TOK_KW_DYN:
        return true;
    default:
        return false;
    }
}

// The position of `=>` when a closure starts here, else 0.
static size_t closure_arrow(const Parser *p) {
    if (p->no_struct_literal) {
        return 0;
    }
    size_t i = at_word(p, p->words.move) && peek_at(p, 1) != TOK_FAT_ARROW ? 1 : 0;
    if (peek_at(p, i) == TOK_IDENT) {
        return peek_at(p, i + 1) == TOK_FAT_ARROW ? i + 1 : 0;
    }
    if (peek_at(p, i) != TOK_LPAREN) {
        return 0;
    }
    i++;
    for (int depth = 1; depth > 0; i++) {
        TokenKind kind = peek_at(p, i);
        if (kind == TOK_EOF || kind == TOK_LBRACE || kind == TOK_RBRACE) {
            return 0;
        }
        depth += kind == TOK_LPAREN ? 1 : kind == TOK_RPAREN ? -1 : 0;
    }
    if (peek_at(p, i) == TOK_FAT_ARROW) {
        return i;
    }
    if (!type_start(peek_at(p, i))) {
        return 0;
    }
    for (;; i++) {
        TokenKind kind = peek_at(p, i);
        if (kind == TOK_FAT_ARROW) {
            return i;
        }
        if (kind == TOK_EOF || kind == TOK_NEWLINE || kind == TOK_LBRACE || kind == TOK_RBRACE ||
            kind == TOK_EQ) {
            return 0;
        }
    }
}

// Reports the closure, then parses its body to recover.
static Expr *closure(Parser *p, size_t arrow) {
    Span start = peek_token(p, 0)->span;
    syntax_error(p, start, "closures are not supported by stage 0");
    for (size_t i = 0; i <= arrow; i++) {
        advance(p);
    }
    skip_newlines(p);
    parse_expr(p);
    return new_expr(p, EXPR_TUPLE, start);
}


// ---------------------------------------------------------------------------
// Primary and postfix

Expr *parse_primary(Parser *p) {
    Span start = peek_token(p, 0)->span;
    switch (peek(p)) {
    case TOK_INT: case TOK_FLOAT: case TOK_BYTE: case TOK_STR: case TOK_KW_TRUE: case TOK_KW_FALSE:
        return literal_token(p);
    case TOK_STR_START: return interpolation(p);
    case TOK_IDENT: {
        size_t arrow = closure_arrow(p);
        return arrow > 0 ? closure(p, arrow) : name(p);
    }
    case TOK_KW_SELF_VALUE: advance(p); return new_expr(p, EXPR_SELF, start);
    case TOK_KW_SELF_TYPE: advance(p); return new_expr(p, EXPR_SELF_TYPE, start);
    case TOK_LPAREN: {
        size_t arrow = closure_arrow(p);
        return arrow > 0 ? closure(p, arrow) : parenthesized(p);
    }
    case TOK_LBRACKET: return array(p);
    case TOK_DOT: return variant_shorthand(p);
    case TOK_LBRACE: case TOK_KW_IF: case TOK_KW_MATCH: case TOK_KW_UNSAFE:
        return parse_control(p);
    default:
        unexpected(p, "an expression");
        return new_expr(p, EXPR_TUPLE, start);
    }
}

ArgList parse_call_args(Parser *p) {
    bool saved = p->no_struct_literal;
    p->no_struct_literal = false;
    Vec(Arg) items = {0};
    while (!at(p, TOK_RPAREN) && !at(p, TOK_EOF)) {
        Arg arg = {0};
        if (at(p, TOK_IDENT) && peek_at(p, 1) == TOK_COLON) {
            arg.name = advance(p)->as.symbol;
            advance(p);
        }
        arg.value = parse_expr(p);
        VEC_PUSH(items, arg);
        if (!accept(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, TOK_RPAREN, "`)` to close the arguments");
    p->no_struct_literal = saved;
    ArgList list;
    TO_LIST(p, items, list);
    return list;
}

static Expr *tuple_field(Parser *p, Expr *base, uint32_t index) {
    Expr *e = new_expr(p, EXPR_TUPLE_FIELD, base->span);
    e->as.tuple_field.base = base;
    e->as.tuple_field.index = index;
    return e;
}

// After `.`: a field name, a tuple index `0`, or a float token `0.1` that
// the lexer produced for `pair.0.1` (spec 1.7): two tuple indexes.
static Expr *member(Parser *p, Expr *base) {
    const Token *t = advance(p);
    if (t->kind == TOK_INT) {
        return tuple_field(p, base, (uint32_t)t->as.int_value);
    }
    if (t->kind == TOK_FLOAT) {
        const char *text = source_file(&p->session->sources, t->span.file)->text.ptr;
        char *dot;
        unsigned long first = strtoul(text + t->span.start, &dot, 10);
        return tuple_field(p, tuple_field(p, base, (uint32_t)first),
                           (uint32_t)strtoul(dot + 1, NULL, 10));
    }
    if (t->kind != TOK_IDENT) {
        p->pos--;
        unexpected(p, "a field name after `.`");
        return base;
    }
    Expr *e = new_expr(p, EXPR_FIELD, base->span);
    e->as.field.base = base;
    e->as.field.name = t->as.symbol;
    if (at(p, TOK_LT) && looks_like_type_args(p, 0)) {   // x.wrappingCast<u8>()
        advance(p);
        e->as.field.type_args = parse_type_args(p);
    }
    e->span = span_since(p, base->span);
    return e;
}

static Expr *postfix_once(Parser *p, Expr *base) {
    Expr *e;
    switch (peek(p)) {
    case TOK_LPAREN:
        advance(p);
        e = new_expr(p, EXPR_CALL, base->span);
        e->as.call.callee = base;
        e->as.call.args = parse_call_args(p);
        break;
    case TOK_LBRACKET:
        advance(p);
        e = new_expr(p, EXPR_INDEX, base->span);
        e->as.index.base = base;
        e->as.index.index = parse_expr(p);
        expect(p, TOK_RBRACKET, "`]` after the index");
        break;
    case TOK_DOT: advance(p); skip_newlines(p); e = member(p, base); break;
    case TOK_QUESTION:
        advance(p);
        e = new_expr(p, EXPR_TRY, base->span);
        e->as.inner = base;
        break;
    default:
        return NULL;
    }
    e->span = span_since(p, base->span);
    return e;
}

Expr *parse_postfix(Parser *p, Expr *base) {
    int steps = 0;
    for (;;) {
        if (at(p, TOK_LBRACE) && !p->no_struct_literal && is_type_name(p, base)) {
            base = struct_literal(p, base);
        } else {
            Expr *next = postfix_once(p, base);
            if (!next || next == base) {
                break;
            }
            base = next;
        }
        steps++;   // a chain is as deep as it is long
        if (!nest(p)) {
            break;
        }
    }
    p->depth -= steps;
    return base;
}
