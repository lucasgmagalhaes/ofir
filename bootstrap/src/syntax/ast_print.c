// `--emit=ast`: one node per line, children indented by two spaces. Types
// and patterns are printed inline in source-like form (ast_text.c).

#include "syntax/ast_print_internal.h"
#include "syntax/ast_text.h"


void line_start(Printer *p) {
    for (int i = 0; i < p->depth; i++) {
        fputs("  ", p->out);
    }
}

void line(Printer *p, const char *text) {
    line_start(p);
    fprintf(p->out, "%s\n", text);
}

void text_line(Printer *p, const char *label, const StrBuf *text) {
    line_start(p);
    fprintf(p->out, "%s %s\n", label, text->data ? text->data : "");
}

void type_line(Printer *p, const char *label, const TypeExpr *t) {
    if (!t) {
        return;
    }
    StrBuf buf = {0};
    type_text(&buf, t, p->names);
    text_line(p, label, &buf);
    strbuf_free(&buf);
}

void pattern_line(Printer *p, const char *label, const Pattern *pat) {
    StrBuf buf = {0};
    pattern_text(&buf, pat, p->names);
    text_line(p, label, &buf);
    strbuf_free(&buf);
}

const char *name(const Printer *p, Symbol s) {
    return symbol_name(p->names, s).ptr;
}

// A node with children: prints `label`, then the children one level deeper.
static void child(Printer *p, const char *label, const Expr *e) {
    if (!e) {
        return;
    }
    line(p, label);
    p->depth++;
    print_expr(p, e);
    p->depth--;
}

static const char *op_text(Op op) {
    static const char *const texts[] = {
        [OP_ADD] = "+", [OP_SUB] = "-", [OP_MUL] = "*", [OP_DIV] = "/", [OP_REM] = "%",
        [OP_AND] = "&", [OP_OR] = "|", [OP_XOR] = "^", [OP_SHL] = "<<", [OP_SHR] = ">>",
        [OP_EQ] = "==", [OP_NE] = "!=", [OP_LT] = "<", [OP_LE] = "<=", [OP_GT] = ">",
        [OP_GE] = ">=", [OP_LOGICAL_AND] = "&&", [OP_LOGICAL_OR] = "||", [OP_COALESCE] = "??",
        [OP_NEG] = "-", [OP_NOT] = "!", [OP_DEREF] = "*", [OP_REF] = "&", [OP_REF_MUT] = "&mut",
        [OP_ASSIGN] = "=",
    };
    return texts[op];
}

static void args(Printer *p, const Arg *items, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (items[i].name.id) {
            line_start(p);
            fprintf(p->out, "arg %s\n", name(p, items[i].name));
            p->depth++;
            print_expr(p, items[i].value);
            p->depth--;
        } else {
            print_expr(p, items[i].value);
        }
    }
}

static void expr_list(Printer *p, ExprList list) {
    for (uint32_t i = 0; i < list.len; i++) {
        print_expr(p, list.items[i]);
    }
}

static void struct_literal(Printer *p, const Expr *e) {
    line(p, "struct");
    p->depth++;
    print_expr(p, e->as.struct_.type);
    for (uint32_t i = 0; i < e->as.struct_.fields.len; i++) {
        const FieldInit *f = &e->as.struct_.fields.items[i];
        line_start(p);
        fprintf(p->out, "field %s\n", name(p, f->name));
        p->depth++;
        if (f->value) {
            print_expr(p, f->value);
        }
        p->depth--;
    }
    child(p, "base", e->as.struct_.base);
    p->depth--;
}

static void if_expr(Printer *p, const Expr *e) {
    line(p, "if");
    p->depth++;
    if (e->as.if_.let_pattern) {
        pattern_line(p, "const", e->as.if_.let_pattern);
    }
    print_expr(p, e->as.if_.cond);
    print_block(p, "then", e->as.if_.then);
    const Expr *else_ = e->as.if_.else_;
    if (else_ && else_->kind == EXPR_BLOCK) {
        print_block(p, "else", else_->as.block);
    } else {
        child(p, "else", else_);   // `else if`
    }
    p->depth--;
}

static void match_expr(Printer *p, const Expr *e) {
    line(p, "match");
    p->depth++;
    print_expr(p, e->as.match.scrutinee);
    for (uint32_t i = 0; i < e->as.match.arms.len; i++) {
        const MatchArm *arm = &e->as.match.arms.items[i];
        pattern_line(p, "arm", arm->pattern);
        p->depth++;
        child(p, "guard", arm->guard);
        print_expr(p, arm->body);
        p->depth--;
    }
    p->depth--;
}

// Leaf expressions print on one line; returns false for other kinds.
static bool leaf(Printer *p, const Expr *e) {
    StrBuf buf = {0};
    switch (e->kind) {
    case EXPR_INT: case EXPR_FLOAT: case EXPR_BYTE: case EXPR_BOOL: case EXPR_STR:
        literal_text(&buf, e, p->names);
        text_line(p, e->kind == EXPR_STR ? "str" : "lit", &buf);
        break;
    case EXPR_IDENT:
        line_start(p);
        fprintf(p->out, "ident %s\n", name(p, e->as.ident.name));
        break;
    case EXPR_SELF: line(p, "self"); break;
    case EXPR_SELF_TYPE: line(p, "Self"); break;
    default:
        strbuf_free(&buf);
        return false;
    }
    strbuf_free(&buf);
    return true;
}

static void generic(Printer *p, const Expr *e) {
    StrBuf buf = {0};
    strbuf_append(&buf, symbol_name(p->names, e->as.generic.name));
    strbuf_push(&buf, '<');
    for (uint32_t i = 0; i < e->as.generic.args.len; i++) {
        strbuf_append_cstr(&buf, i > 0 ? ", " : "");
        type_text(&buf, e->as.generic.args.items[i], p->names);
    }
    strbuf_push(&buf, '>');
    text_line(p, "generic", &buf);
    strbuf_free(&buf);
}

static void with_children(Printer *p, const char *label, const Expr *a, const Expr *b) {
    line(p, label);
    p->depth++;
    if (a) print_expr(p, a);
    if (b) print_expr(p, b);
    p->depth--;
}

static void compound(Printer *p, const Expr *e) {
    char label[64];
    switch (e->kind) {
    case EXPR_UNARY:
        snprintf(label, sizeof label, "unary %s", op_text(e->as.unary.op));
        with_children(p, label, e->as.unary.operand, NULL);
        break;
    case EXPR_BINARY:
        snprintf(label, sizeof label, "binary %s", op_text(e->as.binary.op));
        with_children(p, label, e->as.binary.lhs, e->as.binary.rhs);
        break;
    case EXPR_CAST:
        line(p, "cast");
        p->depth++;
        print_expr(p, e->as.cast.value);
        type_line(p, "to", e->as.cast.type);
        p->depth--;
        break;
    case EXPR_RANGE:
        // `_` marks a missing bound: `range _..` is `..hi`, `range .._` is `lo..`.
        snprintf(label, sizeof label, "range %s%s%s", e->as.range.lo ? "" : "_",
                 e->as.range.inclusive ? "..=" : "..", e->as.range.hi ? "" : "_");
        with_children(p, label, e->as.range.lo, e->as.range.hi);
        break;
    case EXPR_CALL:
        line(p, "call");
        p->depth++;
        print_expr(p, e->as.call.callee);
        args(p, e->as.call.args.items, e->as.call.args.len);
        p->depth--;
        break;
    case EXPR_INDEX: with_children(p, "index", e->as.index.base, e->as.index.index); break;
    case EXPR_FIELD: {
        StrBuf buf = {0};
        strbuf_appendf(&buf, "field %s", name(p, e->as.field.name));
        for (uint32_t i = 0; i < e->as.field.type_args.len; i++) {
            strbuf_append_cstr(&buf, i == 0 ? "<" : ", ");
            type_text(&buf, e->as.field.type_args.items[i], p->names);
        }
        strbuf_append_cstr(&buf, e->as.field.type_args.len ? ">" : "");
        with_children(p, buf.data, e->as.field.base, NULL);
        strbuf_free(&buf);
        break;
    }
    case EXPR_TUPLE_FIELD:
        snprintf(label, sizeof label, "tuple_field %u", e->as.tuple_field.index);
        with_children(p, label, e->as.tuple_field.base, NULL);
        break;
    case EXPR_TRY: with_children(p, "try", e->as.inner, NULL); break;
    default: break;   // GCOVR_EXCL_LINE: every expression kind has its case
    }
}

static void sequence(Printer *p, const char *label, ExprList list) {
    line(p, label);
    p->depth++;
    expr_list(p, list);
    p->depth--;
}

static void variant(Printer *p, const Expr *e) {
    line_start(p);
    fprintf(p->out, "variant .%s%s\n", name(p, e->as.variant.name),
            e->as.variant.has_args ? "()" : "");
    p->depth++;
    args(p, e->as.variant.args.items, e->as.variant.args.len);
    p->depth--;
}

void print_expr(Printer *p, const Expr *e) {
    if (!e) {   // missing after a syntax error (`const x` without `= value`)
        line(p, "<missing>");
        return;
    }
    if (leaf(p, e)) {
        return;
    }
    switch (e->kind) {
    case EXPR_INTERP: sequence(p, "interp", e->as.parts); break;
    case EXPR_GENERIC: generic(p, e); break;
    case EXPR_STRUCT: struct_literal(p, e); break;
    case EXPR_ARRAY: sequence(p, "array", e->as.elems); break;
    case EXPR_TUPLE: sequence(p, "tuple", e->as.elems); break;
    case EXPR_ARRAY_REPEAT:
        with_children(p, "repeat", e->as.repeat.value, e->as.repeat.count);
        break;
    case EXPR_VARIANT: variant(p, e); break;
    case EXPR_IF: if_expr(p, e); break;
    case EXPR_MATCH: match_expr(p, e); break;
    case EXPR_BLOCK: print_block(p, "block", e->as.block); break;
    case EXPR_UNSAFE: print_block(p, "unsafe", e->as.block); break;
    default: compound(p, e); break;
    }
}

// ---------------------------------------------------------------------------
// Statements

static void for_stmt(Printer *p, const Stmt *s) {
    line_start(p);
    switch (s->as.for_.kind) {
    case FOR_INFINITE: fputs("for\n", p->out); break;
    case FOR_COND: fputs("for cond\n", p->out); break;
    case FOR_IN:
        fprintf(p->out, "for %s%s", s->as.for_.mutable ? "mut " : "", name(p, s->as.for_.first));
        if (s->as.for_.second.id) {
            fprintf(p->out, ", %s", name(p, s->as.for_.second));
        }
        fputs(" in\n", p->out);
        break;
    }
    p->depth++;
    if (s->as.for_.expr) {
        print_expr(p, s->as.for_.expr);
    }
    print_block(p, "body", s->as.for_.body);
    p->depth--;
}

static void stmt(Printer *p, const Stmt *s) {
    switch (s->kind) {
    case STMT_LET:
        pattern_line(p, "const", s->as.let.pattern);
        p->depth++;
        type_line(p, "type", s->as.let.type);
        print_expr(p, s->as.let.value);
        if (s->as.let.else_) {
            print_block(p, "else", s->as.let.else_);
        }
        p->depth--;
        break;
    case STMT_VAR:
        line_start(p);
        fprintf(p->out, "let %s\n", name(p, s->as.var.name));
        p->depth++;
        type_line(p, "type", s->as.var.type);
        if (s->as.var.value) {
            print_expr(p, s->as.var.value);
        }
        p->depth--;
        break;
    case STMT_ASSIGN: {
        char label[32];
        const char *op = s->as.assign.op == OP_ASSIGN ? "" : op_text(s->as.assign.op);
        snprintf(label, sizeof label, "assign %s=", op);
        with_children(p, label, s->as.assign.target, s->as.assign.value);
        break;
    }
    case STMT_FOR: for_stmt(p, s); break;
    case STMT_RETURN: with_children(p, "return", s->as.expr, NULL); break;
    case STMT_BREAK: line(p, "break"); break;
    case STMT_CONTINUE: line(p, "continue"); break;
    case STMT_EXPR: print_expr(p, s->as.expr); break;
    }
}

void print_block(Printer *p, const char *label, const Block *b) {
    line(p, label);
    p->depth++;
    for (uint32_t i = 0; i < b->stmts.len; i++) {
        stmt(p, b->stmts.items[i]);
    }
    p->depth--;
}
