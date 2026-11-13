// Operator expressions: precedence climbing over the table of spec 4.8.
// Primary and postfix expressions are in parse_primary.c.

#include "syntax/parser_internal.h"

Expr *new_expr(Parser *p, ExprKind kind, Span start) {
    Expr *e = NEW(p, Expr);
    e->kind = kind;
    e->span = span_since(p, start);
    return e;
}

static Expr *binary(Parser *p, Op op, Expr *lhs, Expr *rhs) {
    Expr *e = new_expr(p, EXPR_BINARY, lhs->span);
    e->as.binary.op = op;
    e->as.binary.lhs = lhs;
    e->as.binary.rhs = rhs;
    return e;
}

// Two tokens with no space between them, such as the `>` `>` of a shift.
static bool adjacent(const Parser *p, size_t ahead) {
    return peek_token(p, ahead)->span.end == peek_token(p, ahead + 1)->span.start;
}

// The lexer emits `>` alone (spec 1.6): `>>`, `>=` and `>>=` are joined
// here. Returns the number of tokens forming the operator at the cursor.
static int gt_operator(const Parser *p, Op *op) {
    bool shift = peek_at(p, 1) == TOK_GT && adjacent(p, 0);
    if (shift) {
        bool assign = peek_at(p, 2) == TOK_EQ && adjacent(p, 1);
        *op = OP_SHR;
        return assign ? 0 : 2;   // `>>=` is an assignment, not an operator
    }
    if (peek_at(p, 1) == TOK_EQ && adjacent(p, 0)) {
        bool equality = peek_at(p, 2) == TOK_EQ && adjacent(p, 1);
        *op = OP_GE;
        return equality ? 0 : 2;  // `>==` is not an operator
    }
    *op = OP_GT;
    return 1;
}

typedef enum {
    LEVEL_NONE = 0,
    LEVEL_OR = 2,         // ||
    LEVEL_AND = 3,        // &&
    LEVEL_COMPARE = 4,    // == != < <= > >=
    LEVEL_ADD = 5,        // + - | ^
    LEVEL_MUL = 6,        // * / % << >> &
} Level;

// The binary operator at the cursor: its level, and in *op the operator and
// in *count the number of tokens it spans. LEVEL_NONE if there is none.
static Level operator_at(const Parser *p, Op *op, int *count) {
    *count = 1;
    switch (peek(p)) {
    case TOK_OR_OR: *op = OP_LOGICAL_OR; return LEVEL_OR;
    case TOK_AND_AND: *op = OP_LOGICAL_AND; return LEVEL_AND;
    case TOK_EQ_EQ: *op = OP_EQ; return LEVEL_COMPARE;
    case TOK_NE: *op = OP_NE; return LEVEL_COMPARE;
    case TOK_LT: *op = OP_LT; return LEVEL_COMPARE;
    case TOK_LE: *op = OP_LE; return LEVEL_COMPARE;
    case TOK_PLUS: *op = OP_ADD; return LEVEL_ADD;
    case TOK_MINUS: *op = OP_SUB; return LEVEL_ADD;
    case TOK_PIPE: *op = OP_OR; return LEVEL_ADD;
    case TOK_CARET: *op = OP_XOR; return LEVEL_ADD;
    case TOK_STAR: *op = OP_MUL; return LEVEL_MUL;
    case TOK_SLASH: *op = OP_DIV; return LEVEL_MUL;
    case TOK_PERCENT: *op = OP_REM; return LEVEL_MUL;
    case TOK_SHL: *op = OP_SHL; return LEVEL_MUL;
    case TOK_AMP: *op = OP_AND; return LEVEL_MUL;
    case TOK_GT:
        *count = gt_operator(p, op);
        if (*count == 0) {
            return LEVEL_NONE;
        }
        return *op == OP_SHR ? LEVEL_MUL : LEVEL_COMPARE;
    default: return LEVEL_NONE;
    }
}

static Expr *unary(Parser *p);

static Expr *cast(Parser *p) {
    Expr *e = unary(p);
    int steps = 0;
    while (accept(p, TOK_KW_AS)) {
        steps++;
        if (!nest(p)) {
            break;
        }
        Expr *c = new_expr(p, EXPR_CAST, e->span);
        c->as.cast.value = e;
        c->as.cast.type = parse_simple_type(p);
        c->span = span_since(p, e->span);
        e = c;
    }
    p->depth -= steps;
    return e;
}

// Precedence climbing: the operators of `min` and above, left-associative.
// One table lookup per operator instead of one call per level.
static Expr *binary_expr(Parser *p, Level min) {
    Expr *lhs = cast(p);
    int steps = 0;
    for (;;) {
        Op op;
        int count;
        Level lv = operator_at(p, &op, &count);
        if (lv == LEVEL_NONE || lv < min) {
            p->depth -= steps;
            return lhs;
        }
        p->pos += (size_t)count;
        steps++;
        if (!nest(p)) {   // a chain is as deep as it is long
            p->depth -= steps;
            return lhs;
        }
        skip_newlines(p);   // an operator at the end of a line continues it
        Expr *rhs = binary_expr(p, lv + 1);
        lhs = binary(p, op, lhs, rhs);
        Op next;
        if (lv == LEVEL_COMPARE && operator_at(p, &next, &count) == LEVEL_COMPARE) {
            syntax_error(p, peek_token(p, 0)->span,
                         "comparison operators cannot be chained; use `&&`");
        }
    }
}

static Expr *coalesce(Parser *p) {
    Expr *lhs = binary_expr(p, LEVEL_OR);
    if (!accept(p, TOK_QUESTION_QUESTION)) {
        return lhs;
    }
    skip_newlines(p);
    return binary(p, OP_COALESCE, lhs, coalesce(p));   // right-associative
}

static bool can_end_range(const Parser *p) {
    switch (peek(p)) {
    case TOK_NEWLINE: case TOK_EOF: case TOK_RPAREN: case TOK_RBRACKET: case TOK_RBRACE:
    case TOK_COMMA: case TOK_LBRACE:
        return true;
    default:
        return false;
    }
}

Expr *parse_expr(Parser *p) {
    Span start = peek_token(p, 0)->span;
    Expr *lo = at(p, TOK_DOT_DOT) || at(p, TOK_DOT_DOT_EQ) ? NULL : coalesce(p);
    if (!at(p, TOK_DOT_DOT) && !at(p, TOK_DOT_DOT_EQ)) {
        return lo;
    }
    bool inclusive = advance(p)->kind == TOK_DOT_DOT_EQ;
    Expr *hi = can_end_range(p) ? NULL : coalesce(p);
    Expr *e = new_expr(p, EXPR_RANGE, start);
    e->as.range.lo = lo;
    e->as.range.hi = hi;
    e->as.range.inclusive = inclusive;
    return e;
}

Expr *parse_expr_no_struct(Parser *p) {
    bool saved = p->no_struct_literal;
    p->no_struct_literal = true;
    Expr *e = parse_expr(p);
    p->no_struct_literal = saved;
    return e;
}

static Expr *unary_level(Parser *p);

static Expr *unary(Parser *p) {
    Expr *e = nest(p) ? unary_level(p) : new_expr(p, EXPR_TUPLE, peek_token(p, 0)->span);
    p->depth--;
    return e;
}

static Expr *unary_level(Parser *p) {
    Span start = peek_token(p, 0)->span;
    Op op;
    switch (peek(p)) {
    case TOK_MINUS: op = OP_NEG; break;
    case TOK_BANG: op = OP_NOT; break;
    case TOK_STAR: op = OP_DEREF; break;
    case TOK_AMP: op = peek_at(p, 1) == TOK_KW_MUT ? OP_REF_MUT : OP_REF; break;
    default: return parse_postfix(p, parse_primary(p));
    }
    advance(p);
    if (op == OP_REF_MUT) {
        advance(p);   // mut
    }
    Expr *operand = unary(p);
    Expr *e = new_expr(p, EXPR_UNARY, start);
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}
