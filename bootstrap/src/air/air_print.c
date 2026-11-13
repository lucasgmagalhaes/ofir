// AIR dump for `--emit=air` and tests.

#include "air/air_print.h"

#include <inttypes.h>

typedef struct {
    const Sema *sema;
    const Interner *names;
    FILE *out;
    StrBuf buf;
} AirPrinter;

static StrView def_name(const Sema *sema, uint32_t def) {
    return symbol_name(&sema->session->names, sema->defs.data[def].name);
}

static const char *type_str(AirPrinter *p, TypeId type) {
    strbuf_clear(&p->buf);
    ty_text(&p->buf, &p->sema->types, type, sema_type_name, p->sema);
    return p->buf.data;
}

// `<int, str>` for an instance; nothing when `targs` is ().
static void type_args(AirPrinter *p, TypeId targs) {
    const Ty *ty = type_get(&p->sema->types, targs);
    for (uint32_t i = 0; targs.id != TID_UNIT && i < ty->arg_count; i++) {
        fprintf(p->out, "%s%s", i ? ", " : "<", type_str(p, ty->args[i]));
    }
    fputs(targs.id != TID_UNIT ? ">" : "", p->out);
}

static void string_literal(AirPrinter *p, StrView text) {
    fputc('"', p->out);
    for (size_t i = 0; i < text.len; i++) {
        unsigned char c = (unsigned char)text.ptr[i];
        if (c < 0x20 || c == '"' || c == '\\' || c >= 0x7f) {
            fprintf(p->out, "\\x%02x", c);
        } else {
            fputc(c, p->out);
        }
    }
    fputc('"', p->out);
}

static const char *const OP_NAMES[] = {
    [OP_ADD] = "add", [OP_SUB] = "sub", [OP_MUL] = "mul", [OP_DIV] = "div", [OP_REM] = "rem",
    [OP_AND] = "and", [OP_OR] = "or", [OP_XOR] = "xor", [OP_SHL] = "shl", [OP_SHR] = "shr",
    [OP_EQ] = "eq", [OP_NE] = "ne", [OP_LT] = "lt", [OP_LE] = "le", [OP_GT] = "gt",
    [OP_GE] = "ge", [OP_NEG] = "neg", [OP_NOT] = "not",
};

static const char *const PANIC_NAMES[] = {
    [PANIC_OVERFLOW] = "overflow", [PANIC_DIV_ZERO] = "div_zero",
    [PANIC_SHIFT] = "shift", [PANIC_CAST] = "cast", [PANIC_BOUNDS] = "bounds",
    [PANIC_ASSERT] = "assert",
};

static const char *const FLOAT_FN_NAMES[] = {
    [FF_SQRT] = "sqrt", [FF_FABS] = "fabs", [FF_FLOOR] = "floor", [FF_CEIL] = "ceil",
    [FF_MINNUM] = "minnum", [FF_MAXNUM] = "maxnum",
};

static void operand(AirPrinter *p, Operand o) {
    switch (o.kind) {
    case OPERAND_COPY: case OPERAND_ADDR:
        fprintf(p->out, "%s%s_%u", o.kind == OPERAND_ADDR ? "&" : "", o.path.deref ? "*" : "",
                o.as.local.id);
        for (uint32_t i = 0; i < o.path.len; i++) {
            fprintf(p->out, ".%u", o.path.fields[i]);
        }
        break;
    case OPERAND_INT: fprintf(p->out, "%" PRIu64 "_%s", o.as.int_value, type_str(p, o.type)); break;
    case OPERAND_FLOAT: fprintf(p->out, "%g_%s", o.as.float_value, type_str(p, o.type)); break;
    case OPERAND_BOOL: fprintf(p->out, "%s", o.as.bool_value ? "true" : "false"); break;
    case OPERAND_STR: string_literal(p, o.as.str); break;
    case OPERAND_UNIT: fputs("()", p->out); break;
    }
}

static void operands(AirPrinter *p, const char *name, Operand a, const Operand *b) {
    fprintf(p->out, "%s ", name);
    operand(p, a);
    if (b) {
        fputs(", ", p->out);
        operand(p, *b);
    }
}

static void rvalue(AirPrinter *p, const Rvalue *rv) {
    switch (rv->kind) {
    case RV_STR_LEN: operands(p, "str_len", rv->a, NULL); return;
    case RV_PTR_ADD: operands(p, "ptr_add", rv->a, &rv->b); return;
    case RV_SIZE_OF: fprintf(p->out, "size_of %s", type_str(p, rv->to)); return;
    case RV_ALIGN_OF: fprintf(p->out, "align_of %s", type_str(p, rv->to)); return;
    case RV_GLOBAL_ADDR:
        fprintf(p->out, "global_addr " SV_FMT, SV_ARG(def_name(p->sema, rv->def)));
        return;
    case RV_AGGREGATE:
        fputs("{", p->out);
        for (uint32_t i = 0; i < rv->count; i++) {
            fputs(i ? ", " : "", p->out);
            operand(p, rv->items[i]);
        }
        fputs("}", p->out);
        return;
    case RV_STR_BYTE: operands(p, "str_byte", rv->a, &rv->b); return;
    case RV_FLOAT_FN:
        operands(p, FLOAT_FN_NAMES[rv->fn], rv->a,
                 rv->fn == FF_MINNUM || rv->fn == FF_MAXNUM ? &rv->b : NULL);
        return;
    case RV_SATURATE:
        operands(p, "saturate", rv->a, NULL);
        fprintf(p->out, " to %s", type_str(p, rv->to));
        return;
    case RV_USE: operand(p, rv->a); return;
    case RV_UNARY: fprintf(p->out, "%s ", OP_NAMES[rv->op]); operand(p, rv->a); return;
    case RV_CAST: case RV_CAST_FAILS:
        fputs(rv->kind == RV_CAST ? "cast " : "cast_fails ", p->out);
        operand(p, rv->a);
        fprintf(p->out, " to %s", type_str(p, rv->to));
        return;
    case RV_BINARY: case RV_CHECKED:
        fprintf(p->out, "%s%s ", rv->kind == RV_CHECKED ? "checked " : "", OP_NAMES[rv->op]);
        operand(p, rv->a);
        fputs(", ", p->out);
        operand(p, rv->b);
        if (rv->kind == RV_CHECKED) {
            fprintf(p->out, " (overflow: _%u)", rv->flag.id);
        }
        return;
    }
}

static void statement(AirPrinter *p, const AirStmt *s) {
    fputs("    ", p->out);
    if (s->has_dest) {
        fprintf(p->out, "_%u", s->dest.id);
        for (uint32_t i = 0; i < s->dest_path.len; i++) {
            fprintf(p->out, ".%u", s->dest_path.fields[i]);
        }
        fputs(" = ", p->out);
    }
    if (s->kind == AIR_ASSIGN) {
        rvalue(p, &s->as.rvalue);
    } else {
        if (s->as.call.runtime) {
            fprintf(p->out, "call %s(", runtime_fn_name(s->as.call.rt));
        } else {
            StrView name = s->as.call.fn.id == GLUE_DROP    ? SV("drop")
                           : s->as.call.fn.id == GLUE_CLONE ? SV("clone")
                                                            : def_name(p->sema, s->as.call.fn.id);
            fprintf(p->out, "call " SV_FMT, SV_ARG(name));
            type_args(p, s->as.call.targs);
            fputc('(', p->out);
        }
        for (uint32_t i = 0; i < s->as.call.arg_count; i++) {
            fputs(i ? ", " : "", p->out);
            operand(p, s->as.call.args[i]);
        }
        fputc(')', p->out);
    }
    fputc('\n', p->out);
}

static void terminator(AirPrinter *p, const Terminator *t) {
    fputs("    ", p->out);
    switch (t->kind) {
    case TERM_NONE: fputs("<unterminated>", p->out); break;
    case TERM_GOTO: fprintf(p->out, "goto bb%u", t->target.id); break;
    case TERM_BRANCH:
        fputs("branch ", p->out);
        operand(p, t->cond);
        fprintf(p->out, ", bb%u, bb%u", t->target.id, t->other.id);
        break;
    case TERM_ASSERT:
        fputs("assert not ", p->out);
        operand(p, t->cond);
        fprintf(p->out, ", %s -> bb%u", PANIC_NAMES[t->panic], t->target.id);
        break;
    case TERM_RETURN: fputs("return", p->out); break;
    case TERM_PANIC: operands(p, "panic", t->cond, NULL); break;
    case TERM_UNREACHABLE: fputs("unreachable", p->out); break;
    }
    fputc('\n', p->out);
}

static void body(AirPrinter *p, const AirBody *b) {
    fprintf(p->out, "fn " SV_FMT, SV_ARG(symbol_name(p->names, b->name)));
    type_args(p, b->targs);
    fprintf(p->out, "%s {\n", b->is_main ? " (entry)" : "");
    for (size_t i = 0; i < b->locals.len; i++) {
        const AirLocal *local = &b->locals.data[i];
        const char *role = i == 0 ? "return" : i <= b->param_count ? "param" : "let";
        fprintf(p->out, "    %s _%zu: %s", role, i, type_str(p, local->type));
        if (local->name.id != 0) {
            fprintf(p->out, "   // " SV_FMT, SV_ARG(symbol_name(p->names, local->name)));
        }
        fputc('\n', p->out);
    }
    for (size_t i = 0; i < b->blocks.len; i++) {
        const AirBlock *block = &b->blocks.data[i];
        fprintf(p->out, "  bb%zu:\n", i);
        for (size_t j = 0; j < block->stmts.len; j++) {
            statement(p, &block->stmts.data[j]);
        }
        terminator(p, &block->term);
    }
    fputs("}\n\n", p->out);
}

void air_print_body(const AirBody *b, const Sema *sema, FILE *out) {
    AirPrinter p = {.sema = sema, .names = &sema->session->names, .out = out};
    body(&p, b);
    strbuf_free(&p.buf);
}
