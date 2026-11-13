// Accumulator recursion (air/accumulate.h, ADR 0044).
//
// The site is a block that ends with
//     _c = call f(args)
//     _r = checked add x, _c (overflow: _o)
//     assert not _o, overflow -> ...
// and whose assertion leads, through copies of _r and gotos, to a return of
// it. The terms x of the calls a recursion makes are summed from the
// innermost call out: `x1 + (x2 + ... + (xm + v))` overflows exactly when
// some suffix sum leaves the range. While every term is nonnegative that
// happens exactly when P + v > MAX, P being the exact sum x1 + ... + xm, so
// the loop keeps P in an accumulator of type T and each return checks once.
// A term that is negative or would take P above MAX takes the original call.
// Calls and their effects keep their order, and the check fails where the
// first failing addition would have.

#include "air/accumulate.h"

#include <stdlib.h>

static const uint32_t INT_BITS[] = {8, 16, 32, 64, 8, 16, 32, 64};

static bool scalar(const TypeTable *types, TypeId type) {
    switch (type_get(types, type)->kind) {
    case TY_UNIT: case TY_NEVER: case TY_BOOL: case TY_INT: case TY_FLOAT:
        return true;
    default:
        return false;
    }
}

static bool whole(Operand o) {
    return o.path.len == 0 && !o.path.deref;
}

// Counts the reads of `o` into `reads`; false if it takes an address.
static bool read(uint32_t *reads, Operand o) {
    if (o.kind == OPERAND_ADDR) {
        return false;
    }
    if (o.kind == OPERAND_COPY) {
        reads[o.as.local.id]++;
    }
    return true;
}

static bool simple_rvalue(RvalueKind kind) {
    switch (kind) {
    case RV_USE: case RV_BINARY: case RV_CHECKED: case RV_UNARY: case RV_CAST:
    case RV_CAST_FAILS: case RV_SATURATE:
        return true;
    default:
        return false;
    }
}

// Counts the reads of every local; false if the body has a statement the
// transformation cannot keep across iterations.
static bool count_reads(const AirBody *body, uint32_t *reads) {
    for (size_t b = 0; b < body->blocks.len; b++) {
        const AirBlock *block = &body->blocks.data[b];
        for (size_t i = 0; i < block->stmts.len; i++) {
            const AirStmt *s = &block->stmts.data[i];
            if (s->dest_path.len || s->dest_path.deref) {
                return false;
            }
            if (s->kind == AIR_CALL) {
                for (uint32_t a = 0; a < s->as.call.arg_count; a++) {
                    if (!read(reads, s->as.call.args[a])) {
                        return false;
                    }
                }
                continue;
            }
            const Rvalue *rv = &s->as.rvalue;
            if (!simple_rvalue(rv->kind) || !read(reads, rv->a) || !read(reads, rv->b)) {
                return false;
            }
        }
        const Terminator *t = &block->term;
        if (!read(reads, t->cond) || !read(reads, t->index) || !read(reads, t->len)) {
            return false;
        }
    }
    return true;
}

// Whether `local`, the value the assertion of the site guards, reaches a
// return from `block` through copies and gotos only.
static bool returns(const AirBody *body, uint32_t local, uint32_t block) {
    for (size_t steps = 0; steps <= body->blocks.len; steps++) {
        const AirBlock *b = &body->blocks.data[block];
        for (size_t i = 0; i < b->stmts.len; i++) {
            const AirStmt *s = &b->stmts.data[i];
            const Rvalue *rv = &s->as.rvalue;
            if (s->kind != AIR_ASSIGN || rv->kind != RV_USE || rv->a.kind != OPERAND_COPY
                || rv->a.as.local.id != local || !whole(rv->a)) {
                return false;
            }
            local = s->dest.id;
        }
        if (b->term.kind == TERM_RETURN) {
            return local == 0;
        }
        if (b->term.kind != TERM_GOTO) {
            return false;
        }
        block = b->term.target.id;
    }
    return false;
}

typedef struct {
    uint32_t block;
    Operand term;     // x
    bool negative;    // x may be negative: a branch takes the original call
} Site;

static bool is_site(const AirBody *body, const TypeTable *types, const uint32_t *reads,
                    uint32_t b, Site *site) {
    const AirBlock *block = &body->blocks.data[b];
    size_t n = block->stmts.len;
    if (n < 2 || block->term.kind != TERM_ASSERT || block->term.panic != PANIC_OVERFLOW) {
        return false;
    }
    const AirStmt *call = &block->stmts.data[n - 2], *add = &block->stmts.data[n - 1];
    if (call->kind != AIR_CALL || call->as.call.runtime || !call->has_dest
        || call->as.call.fn.id != body->def.id || call->as.call.targs.id != body->targs.id
        || call->as.call.arg_count != body->param_count) {
        return false;
    }
    const Rvalue *rv = &add->as.rvalue;
    if (add->kind != AIR_ASSIGN || rv->kind != RV_CHECKED || rv->op != OP_ADD) {
        return false;
    }
    uint32_t c = call->dest.id, o = rv->flag.id, r = add->dest.id;
    Operand cond = block->term.cond;
    if (r == 0 || cond.kind != OPERAND_COPY || cond.as.local.id != o || !whole(cond)
        || reads[c] != 1 || reads[o] != 1 || reads[r] != 1) {
        return false;
    }
    bool a_is_call = rv->a.kind == OPERAND_COPY && rv->a.as.local.id == c && whole(rv->a);
    bool b_is_call = rv->b.kind == OPERAND_COPY && rv->b.as.local.id == c && whole(rv->b);
    if (a_is_call == b_is_call) {
        return false;
    }
    Operand x = a_is_call ? rv->b : rv->a;
    const Ty *ty = type_get(types, x.type);
    bool sign = ty->data <= INT_I64;
    if (x.kind == OPERAND_INT) {
        uint32_t width = INT_BITS[ty->data];
        if (sign && (x.as.int_value >> (width - 1)) & 1) {
            return false;
        }
        site->negative = false;
    } else if (x.kind == OPERAND_COPY && whole(x)) {
        site->negative = sign;
    } else {
        return false;
    }
    site->block = b;
    site->term = x;
    return returns(body, r, block->term.target.id);
}

// Whether a return is reachable from the entry without passing through the
// site: without such a base case the recursion never ends, and it must still
// end in a stack overflow rather than loop.
static bool has_base_case(const AirBody *body, uint32_t site) {
    bool *seen = calloc(body->blocks.len, sizeof *seen);
    uint32_t *stack = malloc(body->blocks.len * sizeof *stack);
    size_t top = 0;
    bool found = false;
    stack[top++] = 0;
    seen[0] = true;
    while (top && !found) {
        const Terminator *t = &body->blocks.data[stack[--top]].term;
        found = t->kind == TERM_RETURN;
        uint32_t next[2];
        size_t count = 0;
        if (t->kind == TERM_GOTO || t->kind == TERM_ASSERT || t->kind == TERM_BRANCH) {
            next[count++] = t->target.id;
        }
        if (t->kind == TERM_BRANCH) {
            next[count++] = t->other.id;
        }
        for (size_t i = 0; i < count; i++) {
            if (next[i] != site && !seen[next[i]]) {
                seen[next[i]] = true;
                stack[top++] = next[i];
            }
        }
    }
    free(seen);
    free(stack);
    return found;
}

static Operand copy_of(LocalId local, TypeId type) {
    return (Operand){.kind = OPERAND_COPY, .type = type, .as.local = local};
}

static Operand int_of(uint64_t value, TypeId type) {
    return (Operand){.kind = OPERAND_INT, .type = type, .as.int_value = value};
}

static AirStmt assign(LocalId dest, Rvalue rv) {
    return (AirStmt){.kind = AIR_ASSIGN, .dest = dest, .has_dest = true, .as.rvalue = rv};
}

static AirStmt binary(LocalId dest, Op op, Operand a, Operand b) {
    return assign(dest, (Rvalue){.kind = RV_BINARY, .op = op, .a = a, .b = b});
}

typedef struct {
    AirBody *body;
    TypeId type;                  // T, also the type of acc
    LocalId acc;
    Span span;                    // of the assertion of the site
} Loop;

static LocalId temp(Loop *l, TypeId type) {
    return air_new_local(l->body, type, (Symbol){0}, l->span);
}

static AirStmt checked_add(LocalId dest, Operand a, Operand b, LocalId flag) {
    return assign(dest, (Rvalue){.kind = RV_CHECKED, .op = OP_ADD, .a = a, .b = b,
                                 .flag = flag});
}

// The return of `block` becomes: overflow if acc + v does; else return it.
static void finish(Loop *l, uint32_t block) {
    LocalId sum = temp(l, l->type), overflow = temp(l, type_id(TID_BOOL));
    BlockId at = {block};
    air_push(l->body, at, checked_add(sum, copy_of(l->acc, l->type),
                                      copy_of((LocalId){0}, l->type), overflow));
    BlockId done = air_new_block(l->body);
    air_terminate(l->body, at, (Terminator){.kind = TERM_ASSERT,
                                            .cond = copy_of(overflow, type_id(TID_BOOL)),
                                            .target = done, .panic = PANIC_OVERFLOW,
                                            .span = l->span});
    air_push(l->body, done, assign((LocalId){0}, (Rvalue){.kind = RV_USE,
                                                         .a = copy_of(sum, l->type)}));
    air_terminate(l->body, done, (Terminator){.kind = TERM_RETURN});
}

// The statements that set the parameters to the arguments and jump to the
// old entry `head`.
static void iterate(Loop *l, BlockId block, const AirStmt *call, BlockId head) {
    uint32_t n = call->as.call.arg_count;
    LocalId *args = malloc((n + 1) * sizeof *args);
    for (uint32_t i = 0; i < n; i++) {
        args[i] = temp(l, l->body->locals.data[i + 1].type);
        air_push(l->body, block, assign(args[i], (Rvalue){.kind = RV_USE,
                                                          .a = call->as.call.args[i]}));
    }
    for (uint32_t i = 0; i < n; i++) {
        TypeId type = l->body->locals.data[i + 1].type;
        air_push(l->body, block, assign((LocalId){i + 1}, (Rvalue){.kind = RV_USE,
                                                                  .a = copy_of(args[i], type)}));
    }
    free(args);
    air_terminate(l->body, block, (Terminator){.kind = TERM_GOTO, .target = head});
}

static void retarget(Terminator *t, uint32_t from, uint32_t to) {
    if (t->kind == TERM_GOTO || t->kind == TERM_BRANCH || t->kind == TERM_ASSERT) {
        if (t->target.id == from) {
            t->target.id = to;
        }
    }
    if (t->kind == TERM_BRANCH && t->other.id == from) {
        t->other.id = to;
    }
}

static void transform(AirBody *body, Site site) {
    TypeId type = body->locals.data[0].type, b = type_id(TID_BOOL);
    Loop l = {.body = body, .type = type};
    l.span = body->blocks.data[site.block].term.span;
    l.acc = temp(&l, type);

    // The old entry moves to `head`; the new entry clears the accumulator.
    BlockId head = air_new_block(body);
    body->blocks.data[head.id] = body->blocks.data[0];
    body->blocks.data[0] = (AirBlock){0};
    for (size_t i = 1; i < body->blocks.len; i++) {
        retarget(&body->blocks.data[i].term, 0, head.id);
    }
    BlockId entry = {0};
    air_push(body, entry, assign(l.acc, (Rvalue){.kind = RV_USE, .a = int_of(0, type)}));
    air_terminate(body, entry, (Terminator){.kind = TERM_GOTO, .target = head});
    uint32_t site_block = site.block;   // not 0: the entry leads to a base case

    size_t returns_end = body->blocks.len;
    for (size_t i = 1; i < returns_end; i++) {
        if (body->blocks.data[i].term.kind == TERM_RETURN) {
            finish(&l, (uint32_t)i);
        }
    }

    // The term goes into acc unless it is negative or the sum overflows: the
    // slow path then restores acc and makes the original call and addition.
    AirBlock *block = &body->blocks.data[site_block];
    AirStmt call = block->stmts.data[block->stmts.len - 2];
    AirStmt add = block->stmts.data[block->stmts.len - 1];
    Terminator check = block->term;
    block->stmts.len -= 2;
    BlockId at = {site_block};
    LocalId sum = temp(&l, type), bad = temp(&l, b);
    air_push(body, at, checked_add(sum, copy_of(l.acc, type), site.term, bad));
    if (site.negative) {
        LocalId negative = temp(&l, b), overflow = bad;
        bad = temp(&l, b);
        air_push(body, at, binary(negative, OP_LT, site.term, int_of(0, type)));
        air_push(body, at, binary(bad, OP_OR, copy_of(overflow, b), copy_of(negative, b)));
    }
    air_push(body, at, assign(l.acc, (Rvalue){.kind = RV_USE, .a = copy_of(sum, type)}));
    BlockId slow = air_new_block(body), fast = air_new_block(body);
    air_terminate(body, at, (Terminator){.kind = TERM_BRANCH, .cond = copy_of(bad, b),
                                         .target = slow, .other = fast});
    air_push(body, slow, binary(l.acc, OP_SUB, copy_of(l.acc, type), site.term));
    air_push(body, slow, call);
    air_push(body, slow, add);
    air_terminate(body, slow, check);
    iterate(&l, fast, &call, head);
}

void air_accumulate(AirBody *body, const TypeTable *types) {
    if (body->is_main || is_glue(body->def) || body->param_count == 0
        || type_get(types, body->locals.data[0].type)->kind != TY_INT) {
        return;
    }
    for (size_t i = 0; i < body->locals.len; i++) {
        if (body->locals.data[i].by_ref || !scalar(types, body->locals.data[i].type)) {
            return;
        }
    }
    uint32_t *reads = calloc(body->locals.len, sizeof *reads);
    Site site = {0}, found = {0};
    uint32_t sites = 0;
    if (count_reads(body, reads)) {
        for (uint32_t b = 0; b < body->blocks.len; b++) {
            if (is_site(body, types, reads, b, &site)) {
                found = site;
                sites++;
            }
        }
    }
    free(reads);
    if (sites == 1 && found.block != 0 && has_base_case(body, found.block)) {
        transform(body, found);
    }
}
