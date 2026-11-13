// Blocks, `if`, `match` and `unsafe` (spec 4.1, 4.3, 4.5, 4.6).

#include "syntax/parser_internal.h"

Block *parse_block(Parser *p) {
    Span start = peek_token(p, 0)->span;
    Block *b = NEW(p, Block);
    if (!expect(p, TOK_LBRACE, "`{` to start a block")) {
        b->span = start;
        return b;
    }
    bool saved = p->no_struct_literal;
    p->no_struct_literal = false;
    Vec(Stmt *) stmts = {0};
    skip_newlines(p);
    while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
        VEC_PUSH(stmts, parse_stmt(p));
        if (p->recovering) {
            synchronize(p);
        } else {
            expect_terminator(p);
        }
    }
    expect(p, TOK_RBRACE, "`}` to close the block");
    p->no_struct_literal = saved;
    TO_LIST(p, stmts, b->stmts);
    b->span = span_since(p, start);
    return b;
}

static Expr *block_expr(Parser *p, ExprKind kind, Span start) {
    Block *b = parse_block(p);
    Expr *e = new_expr(p, kind, start);
    e->as.block = b;
    return e;
}

// `if cond { } else if ... else { }` and `if const pattern = value { }`.
static Expr *if_expr(Parser *p) {
    Span start = advance(p)->span;
    Expr *e = new_expr(p, EXPR_IF, start);
    if (accept(p, TOK_KW_CONST)) {
        e->as.if_.let_pattern = parse_test_pattern(p);
        expect(p, TOK_EQ, "`=` after the pattern");
    }
    e->as.if_.cond = parse_expr_no_struct(p);
    e->as.if_.then = parse_block(p);
    if (accept(p, TOK_KW_ELSE)) {
        Span else_start = peek_token(p, 0)->span;
        if (!at(p, TOK_KW_IF)) {
            e->as.if_.else_ = block_expr(p, EXPR_BLOCK, else_start);
        } else {
            if (nest(p)) {   // an `else if` chain is as deep as it is long
                e->as.if_.else_ = if_expr(p);
            }
            p->depth--;
        }
    }
    e->span = span_since(p, start);
    return e;
}

static MatchArm match_arm(Parser *p) {
    MatchArm arm = {.pattern = parse_test_pattern(p)};
    if (accept(p, TOK_KW_IF)) {
        arm.guard = parse_expr_no_struct(p);
    }
    expect(p, TOK_FAT_ARROW, "`=>` after the pattern");
    skip_newlines(p);
    arm.body = parse_expr(p);
    return arm;
}

static Expr *match_expr(Parser *p) {
    Span start = advance(p)->span;
    Expr *e = new_expr(p, EXPR_MATCH, start);
    e->as.match.scrutinee = parse_expr_no_struct(p);
    Vec(MatchArm) arms = {0};
    if (expect(p, TOK_LBRACE, "`{` after the matched value")) {
        skip_newlines(p);
        while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
            VEC_PUSH(arms, match_arm(p));
            if (p->recovering) {
                synchronize(p);
                continue;
            }
            bool separated = accept_separator(p);
            skip_newlines(p);
            if (!separated && !at(p, TOK_RBRACE)) {
                unexpected(p, "a new line or `,` after the match arm");
                synchronize(p);
            }
        }
        expect(p, TOK_RBRACE, "`}` to close the match");
    }
    TO_LIST(p, arms, e->as.match.arms);
    e->span = span_since(p, start);
    return e;
}

Expr *parse_control(Parser *p) {
    Span start = peek_token(p, 0)->span;
    switch (peek(p)) {
    case TOK_KW_IF: return if_expr(p);
    case TOK_KW_MATCH: return match_expr(p);
    case TOK_KW_UNSAFE:
        advance(p);
        return block_expr(p, EXPR_UNSAFE, start);
    default:
        return block_expr(p, EXPR_BLOCK, start);
    }
}
