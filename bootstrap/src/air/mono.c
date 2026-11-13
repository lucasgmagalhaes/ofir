// Monomorphization (docs/design/compiler-architecture.md, 10): each
// instance `(fn, type arguments)` reachable from the roots is lowered once.
// Its body is lowered from the generic function, with TY_PARAM types, then
// the type arguments are substituted into every type of the body; the
// calls it makes then name concrete instances, which are reached in turn.

#include "air/lower_internal.h"

#include <stdlib.h>

enum { DEPTH_LIMIT = 64 };   // spec 8.6

static uint64_t instance_key(DefId def, TypeId targs) {
    return (uint64_t)def.id << 32 | targs.id;
}

static uint32_t slot_of(uint64_t key, uint32_t cap) {
    uint64_t h = key * 0x9e3779b97f4a7c15ull;
    return (uint32_t)(h >> 32) & (cap - 1);
}

static void grow(Lowerer *l) {
    uint32_t cap = l->instance_cap ? l->instance_cap * 2 : 64;
    uint64_t *slots = xcalloc(cap, sizeof(uint64_t));
    for (uint32_t i = 0; i < l->instance_cap; i++) {
        uint64_t key = l->instances[i];
        if (key != 0) {
            uint32_t j = slot_of(key, cap);
            while (slots[j] != 0) {
                j = (j + 1) & (cap - 1);
            }
            slots[j] = key;
        }
    }
    free(l->instances);
    l->instances = slots;
    l->instance_cap = cap;
}

// Whether the instance was already reached; records it if not.
static bool seen(Lowerer *l, uint64_t key) {
    if ((l->instance_count + 1) * 2 > l->instance_cap) {
        grow(l);
    }
    uint32_t i = slot_of(key, l->instance_cap);
    for (; l->instances[i] != 0; i = (i + 1) & (l->instance_cap - 1)) {
        if (l->instances[i] == key) {
            return true;
        }
    }
    l->instances[i] = key;
    l->instance_count++;
    return false;
}

// The glue `glue` of the type in `targs`, once (lower_drop.c, lower_clone.c).
static void reach_glue(Lowerer *l, DefId glue, TypeId targs) {
    if (!seen(l, instance_key(glue, targs))) {
        VEC_PUSH(l->pending, ((Instance){NULL, targs, glue}));
    }
}

void reach(Lowerer *l, FnDecl *fn, TypeId targs) {
    if (!fn->body) {
        return;
    }
    if (targs.id == TID_UNIT) {   // not generic: a flag per DefId is enough
        while (l->reached.len <= fn->def.id) {
            VEC_PUSH(l->reached, false);
        }
        if (l->reached.data[fn->def.id]) {
            return;
        }
        l->reached.data[fn->def.id] = true;
    } else if (seen(l, instance_key(fn->def, targs))) {
        return;
    } else if (type_depth(l->types, targs) > DEPTH_LIMIT + 1) {   // + 1: the tuple
        if (!l->too_deep) {
            diag_error(&l->sema->session->diags, "E0331", fn->span, "instantiating `" SV_FMT
                       "` nests types more than %d levels deep: does it call itself with a "
                       "larger type?", SV_ARG(symbol_name(&l->sema->session->names, fn->name)),
                       DEPTH_LIMIT);
        }
        l->too_deep = true;
        return;
    }
    VEC_PUSH(l->pending, ((Instance){fn, targs, {0}}));
}

typedef struct {
    TypeTable *types;
    const TypeId *args;
    uint32_t count;
} Subst;

static void subst_type(const Subst *s, TypeId *type) {
    *type = type_subst(s->types, *type, s->args, s->count);
}

static void subst_operand(const Subst *s, Operand *o) {
    subst_type(s, &o->type);
}

static void subst_rvalue(const Subst *s, Rvalue *rv) {
    subst_operand(s, &rv->a);
    subst_operand(s, &rv->b);
    subst_type(s, &rv->to);
    for (uint32_t i = 0; i < rv->count; i++) {
        subst_operand(s, &rv->items[i]);
    }
}

static void subst_stmt(const Subst *s, AirStmt *stmt) {
    if (stmt->kind == AIR_ASSIGN) {
        subst_rvalue(s, &stmt->as.rvalue);
        return;
    }
    for (uint32_t i = 0; i < stmt->as.call.arg_count; i++) {
        subst_operand(s, &stmt->as.call.args[i]);
    }
    subst_type(s, &stmt->as.call.targs);
}

static void subst_body(Lowerer *l, AirBody *body) {
    const Ty *targs = type_get(l->types, body->targs);
    Subst s = {.types = l->types, .args = targs->args, .count = targs->arg_count};
    for (size_t i = 0; i < body->locals.len; i++) {
        subst_type(&s, &body->locals.data[i].type);
    }
    for (size_t b = 0; b < body->blocks.len; b++) {
        AirBlock *block = &body->blocks.data[b];
        for (size_t i = 0; i < block->stmts.len; i++) {
            subst_stmt(&s, &block->stmts.data[i]);
        }
        subst_operand(&s, &block->term.cond);
        subst_operand(&s, &block->term.index);
        subst_operand(&s, &block->term.len);
    }
}

void mono_body(Lowerer *l, AirBody *body) {
    if (body->targs.id != TID_UNIT) {
        subst_body(l, body);
    }
    for (size_t b = 0; b < body->blocks.len; b++) {
        const AirBlock *block = &body->blocks.data[b];
        for (size_t i = 0; i < block->stmts.len; i++) {
            const AirStmt *stmt = &block->stmts.data[i];
            if (stmt->kind == AIR_CALL && !stmt->as.call.runtime && is_glue(stmt->as.call.fn)) {
                reach_glue(l, stmt->as.call.fn, stmt->as.call.targs);
            } else if (stmt->kind == AIR_CALL && !stmt->as.call.runtime) {
                reach(l, sema_def(l->sema, stmt->as.call.fn)->fn, stmt->as.call.targs);
            }
        }
    }
}

void mono_free(Lowerer *l) {
    VEC_FREE(l->pending);
    VEC_FREE(l->reached);
    free(l->instances);
}
