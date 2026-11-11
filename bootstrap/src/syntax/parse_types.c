// Types (spec appendix A.4), type parameters and parameter modes.

#include "syntax/parser_internal.h"

static TypeExpr *new_type(Parser *p, TypeKind kind, Span start) {
    TypeExpr *t = NEW(p, TypeExpr);
    t->kind = kind;
    t->span = span_since(p, start);
    return t;
}

bool can_start_type(const Parser *p) {
    switch (peek(p)) {
    case TOK_IDENT: case TOK_AMP: case TOK_STAR: case TOK_LBRACKET: case TOK_LPAREN:
    case TOK_KW_FN: case TOK_KW_DYN: case TOK_BANG: case TOK_KW_SELF_TYPE:
        return true;
    default:
        return false;
    }
}

Mode parse_mode(Parser *p) {
    if (accept(p, TOK_KW_MUT)) {
        return MODE_MUT;
    }
    if (at_word(p, p->words.move) && peek_at(p, 1) != TOK_COLON) {
        advance(p);
        return MODE_MOVE;
    }
    return MODE_BORROW;
}

// After `<`: `T, U>`.
TypeList parse_type_args(Parser *p) {
    Vec(TypeExpr *) args = {0};
    skip_newlines(p);
    while (!at(p, TOK_GT) && !at(p, TOK_EOF)) {
        VEC_PUSH(args, parse_type(p));
        skip_newlines(p);
        if (!accept(p, TOK_COMMA)) {
            break;
        }
        skip_newlines(p);
    }
    expect(p, TOK_GT, "`>` to close the type arguments");
    TypeList list;
    TO_LIST(p, args, list);
    return list;
}

TypeList parse_bounds(Parser *p) {
    Vec(TypeExpr *) bounds = {0};
    VEC_PUSH(bounds, parse_type(p));
    while (accept(p, TOK_PLUS)) {
        VEC_PUSH(bounds, parse_type(p));
    }
    TypeList list;
    TO_LIST(p, bounds, list);
    return list;
}

// Inline bounds go to `preds` as `T: A + B`.
SymbolList parse_type_params(Parser *p, WhereVec *preds) {
    Vec(Symbol) params = {0};
    if (accept(p, TOK_LT)) {
        while (at(p, TOK_IDENT)) {
            Span start = peek_token(p, 0)->span;
            Symbol name = advance(p)->as.symbol;
            VEC_PUSH(params, name);
            if (at(p, TOK_COLON)) {
                TypeExpr *t = new_type(p, TYPE_PATH, start);
                t->as.path.segments.items = NEW(p, Symbol);
                t->as.path.segments.items[0] = name;
                t->as.path.segments.len = 1;
                advance(p);   // :
                WherePred pred = {.type = t, .bounds = parse_bounds(p)};
                pred.span = span_since(p, start);
                VEC_PUSH(*preds, pred);
            }
            if (!accept(p, TOK_COMMA)) {
                break;
            }
        }
        expect(p, TOK_GT, "`>` to close the type parameters");
    }
    SymbolList list;
    TO_LIST(p, params, list);
    return list;
}

static TypeExpr *path_type(Parser *p) {
    Span start = peek_token(p, 0)->span;
    Vec(Symbol) segments = {0};
    VEC_PUSH(segments, advance(p)->as.symbol);
    while (at(p, TOK_DOT) && peek_at(p, 1) == TOK_IDENT) {
        advance(p);
        VEC_PUSH(segments, advance(p)->as.symbol);
    }
    TypeList args = {0};
    if (accept(p, TOK_LT)) {
        args = parse_type_args(p);
    }
    TypeExpr *t = new_type(p, TYPE_PATH, start);
    TO_LIST(p, segments, t->as.path.segments);
    t->as.path.args = args;
    return t;
}

// `fn(A, mut B) R` or `move fn(A) R`; the `fn` keyword is next.
static TypeExpr *fn_type(Parser *p, Span start, bool owned) {
    advance(p);   // fn
    expect(p, TOK_LPAREN, "`(` after `fn`");
    Vec(TypeExpr *) params = {0};
    Vec(Mode) modes = {0};
    while (!at(p, TOK_RPAREN) && !at(p, TOK_EOF)) {
        VEC_PUSH(modes, parse_mode(p));
        VEC_PUSH(params, parse_type(p));
        if (!accept(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, TOK_RPAREN, "`)` to close the parameter types");
    TypeExpr *ret = can_start_type(p) ? parse_type(p) : NULL;
    TypeExpr *t = new_type(p, TYPE_FN, start);
    TO_LIST(p, params, t->as.fn.params);
    TO_LIST(p, modes, t->as.fn.modes);
    t->as.fn.ret = ret;
    t->as.fn.owned = owned;
    return t;
}

// `[T]` or `[T; N]`; the `[` is next.
static TypeExpr *bracket_type(Parser *p, Span start) {
    advance(p);
    TypeExpr *elem = parse_type(p);
    Expr *len = accept(p, TOK_SEMICOLON) ? parse_expr(p) : NULL;
    expect(p, TOK_RBRACKET, "`]`");
    TypeExpr *t = new_type(p, len ? TYPE_ARRAY : TYPE_SLICE, start);
    t->as.array.elem = elem;
    t->as.array.len = len;
    return t;
}

// `()`, `(T,)`, `(A, B)`, or `(T)`, which is `T` (`(A | B)` in a type
// test, spec 17.4); the `(` is next.
static TypeExpr *tuple_type(Parser *p, Span start) {
    advance(p);
    Vec(TypeExpr *) elems = {0};
    bool comma = false;
    while (!at(p, TOK_RPAREN) && !at(p, TOK_EOF)) {
        VEC_PUSH(elems, parse_type(p));
        comma = accept(p, TOK_COMMA);
        if (!comma) {
            break;
        }
    }
    expect(p, TOK_RPAREN, "`)` to close the tuple type");
    if (elems.len == 1 && !comma) {
        TypeExpr *inner = elems.data[0];
        VEC_FREE(elems);
        return inner;
    }
    TypeExpr *t = new_type(p, TYPE_TUPLE, start);
    TO_LIST(p, elems, t->as.tuple);
    return t;
}

static TypeExpr *pointer_type(Parser *p, Span start, TypeKind kind) {
    advance(p);   // & or *
    bool mutable = accept(p, TOK_KW_MUT);
    TypeExpr *inner = parse_type(p);
    TypeExpr *t = new_type(p, kind, start);
    t->as.ref.mutable = mutable;
    t->as.ref.inner = inner;
    return t;
}

static TypeExpr *base_type(Parser *p) {
    Span start = peek_token(p, 0)->span;
    switch (peek(p)) {
    case TOK_IDENT:
        if (at_word(p, p->words.move) && peek_at(p, 1) == TOK_KW_FN) {
            advance(p);
            return fn_type(p, start, true);
        }
        return path_type(p);
    case TOK_KW_SELF_TYPE: advance(p); return new_type(p, TYPE_SELF, start);
    case TOK_AMP: return pointer_type(p, start, TYPE_REF);
    case TOK_STAR: return pointer_type(p, start, TYPE_PTR);
    case TOK_LBRACKET: return bracket_type(p, start);
    case TOK_LPAREN: return tuple_type(p, start);
    case TOK_KW_FN: return fn_type(p, start, false);
    case TOK_BANG: advance(p); return new_type(p, TYPE_NEVER, start);
    case TOK_KW_DYN: {
        advance(p);
        TypeExpr *inner = parse_type(p);
        TypeExpr *t = new_type(p, TYPE_DYN, start);
        t->as.inner = inner;
        return t;
    }
    case TOK_LBRACE:   // `{[K in T]: V}`, as stage 1 reads it
        syntax_error(p, start, "mapped types are not supported by stage 0");
        return new_type(p, TYPE_NEVER, start);
    case TOK_KW_IF:   // conditional and `true`/`false` types, as stage 1 reads them
    case TOK_KW_TRUE:
    case TOK_KW_FALSE:
        syntax_error(p, start, "type operators are not supported by stage 0");
        return new_type(p, TYPE_NEVER, start);
    default:
        unexpected(p, "a type");
        return new_type(p, TYPE_NEVER, start);
    }
}

TypeExpr *parse_simple_type(Parser *p) {
    TypeExpr *t = base_type(p);
    while (at(p, TOK_QUESTION)) {   // T? is Option<T>
        advance(p);
        TypeExpr *option = new_type(p, TYPE_OPTION, t->span);
        option->as.inner = t;
        t = option;
    }
    return t;
}

TypeExpr *parse_type(Parser *p) {
    TypeExpr *t = parse_simple_type(p);
    if (!p->recovering && at(p, TOK_LBRACKET)) {   // `T[K]`, as stage 1 reads it
        syntax_error(p, peek_token(p, 0)->span,
                     "indexed access types are not supported by stage 0");
    }
    return t;
}
