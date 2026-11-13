// Structural validation of AIR, run after lowering in debug builds.

#include "air/air_print.h"

#include <stdio.h>

typedef struct {
    const AirBody *body;
    char *error;
    size_t size;
} Validator;

static bool fail(Validator *v, size_t block, const char *what) {
    snprintf(v->error, v->size, "AIR of `fn%u`, bb%zu: %s", v->body->def.id, block, what);
    return false;
}

static bool local_ok(const Validator *v, LocalId id) {
    return id.id < v->body->locals.len;
}

static bool operand_ok(const Validator *v, Operand o) {
    return (o.kind != OPERAND_COPY && o.kind != OPERAND_ADDR) || local_ok(v, o.as.local);
}

static bool stmt_ok(const Validator *v, const AirStmt *s) {
    if (s->has_dest && !local_ok(v, s->dest)) {
        return false;
    }
    if (s->kind == AIR_CALL) {
        for (uint32_t i = 0; i < s->as.call.arg_count; i++) {
            if (!operand_ok(v, s->as.call.args[i])) {
                return false;
            }
        }
        return true;
    }
    const Rvalue *rv = &s->as.rvalue;
    for (uint32_t i = 0; i < rv->count; i++) {
        if (!operand_ok(v, rv->items[i])) {
            return false;
        }
    }
    bool flag_ok = rv->kind != RV_CHECKED || local_ok(v, rv->flag);
    return flag_ok && operand_ok(v, rv->a) && operand_ok(v, rv->b);
}

static bool block_ok(Validator *v, size_t index) {
    const AirBlock *block = &v->body->blocks.data[index];
    for (size_t i = 0; i < block->stmts.len; i++) {
        if (!stmt_ok(v, &block->stmts.data[i])) {
            return fail(v, index, "statement refers to a missing local");
        }
    }
    const Terminator *t = &block->term;
    size_t count = v->body->blocks.len;
    switch (t->kind) {
    case TERM_NONE: return fail(v, index, "block is not terminated");
    case TERM_BRANCH:
        if (t->other.id >= count) {
            return fail(v, index, "branch to a missing block");
        }
        /* fallthrough */
    case TERM_GOTO: case TERM_ASSERT:
        if (t->target.id >= count) {
            return fail(v, index, "jump to a missing block");
        }
        if (!operand_ok(v, t->index) || !operand_ok(v, t->len)) {
            return fail(v, index, "bounds check refers to a missing local");
        }
        return operand_ok(v, t->cond) || fail(v, index, "condition refers to a missing local");
    case TERM_PANIC:
        return operand_ok(v, t->cond) || fail(v, index, "message refers to a missing local");
    case TERM_RETURN: case TERM_UNREACHABLE:
        return true;
    }
    return true;
}

bool air_validate_body(const AirBody *body, char *error, size_t size) {
    Validator v = {.body = body, .error = error, .size = size};
    if (body->locals.len < 1 + body->param_count || body->blocks.len == 0) {
        return fail(&v, 0, "missing return local, parameters or entry block");
    }
    for (size_t b = 0; b < body->blocks.len; b++) {
        if (!block_ok(&v, b)) {
            return false;
        }
    }
    return true;
}
