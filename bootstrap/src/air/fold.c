// Check elimination (air/fold.h). Within a block, a temporary assigned from
// constants is known until the block ends; an assertion on a condition known
// to be false cannot fail and becomes a goto. Named locals and locals whose
// address is taken are never tracked: a call or a closure could write them.

#include "air/fold.h"

#include <stdlib.h>

typedef struct {
    const AirBody *body;
    const TypeTable *types;
    uint32_t *stamp;     // stamp[local] == block + 1: value[local] is known
    uint64_t *value;     // normalized: sign- or zero-extended to 64 bits
    bool *escaped;       // address taken, or a `mut` parameter
    uint32_t block;
} Folder;

static const uint32_t INT_BITS[] = {8, 16, 32, 64, 8, 16, 32, 64};

static const Ty *int_type(const Folder *f, TypeId type) {
    const Ty *ty = type_get(f->types, type);
    return ty->kind == TY_INT ? ty : NULL;
}

static bool is_signed(const Ty *ty) {
    return ty->data <= INT_I64;
}

// `bits` read as a value of `type`: an int is extended from its width.
static uint64_t normalize(const Folder *f, TypeId type, uint64_t bits) {
    const Ty *ty = int_type(f, type);
    if (!ty) {
        return bits & 1;
    }
    uint32_t width = INT_BITS[ty->data];
    if (width == 64) {
        return bits;
    }
    uint64_t mask = (1ull << width) - 1;
    bits &= mask;
    if (is_signed(ty) && (bits >> (width - 1)) & 1) {
        bits |= ~mask;
    }
    return bits;
}

static bool known(const Folder *f, Operand o, uint64_t *out) {
    switch (o.kind) {
    case OPERAND_INT:
        *out = normalize(f, o.type, o.as.int_value);
        return true;
    case OPERAND_BOOL:
        *out = o.as.bool_value;
        return true;
    case OPERAND_COPY:
        if (o.path.len || o.path.deref || f->stamp[o.as.local.id] != f->block + 1) {
            return false;
        }
        *out = f->value[o.as.local.id];
        return true;
    default:
        return false;
    }
}

// Whether the known value `v` of int type `from` lies outside int type `to`.
static bool cast_fails(const Ty *from, const Ty *to, uint64_t v) {
    uint32_t t = INT_BITS[to->data];
    if (is_signed(from) && (int64_t)v < 0) {
        int64_t min = t == 64 ? INT64_MIN : -(int64_t)(1ull << (t - 1));
        return !is_signed(to) || (int64_t)v < min;
    }
    uint64_t max = is_signed(to) ? (1ull << (t - 1)) - 1 : t == 64 ? UINT64_MAX : (1ull << t) - 1;
    return v > max;
}

static bool compare(Op op, uint64_t a, uint64_t b, bool sign, bool *out) {
    bool lt = sign ? (int64_t)a < (int64_t)b : a < b;
    bool gt = sign ? (int64_t)a > (int64_t)b : a > b;
    switch (op) {
    case OP_EQ: *out = a == b; return true;
    case OP_NE: *out = a != b; return true;
    case OP_LT: *out = lt; return true;
    case OP_LE: *out = !gt; return true;
    case OP_GT: *out = gt; return true;
    case OP_GE: *out = !lt; return true;
    default: return false;
    }
}

// `a and b`, `a or b` of bools: one known operand can decide.
static bool logic(const Folder *f, const Rvalue *rv, uint64_t *out) {
    uint64_t a = 0, b = 0;
    bool ka = known(f, rv->a, &a), kb = known(f, rv->b, &b);
    uint64_t absorbing = rv->op == OP_OR;
    if ((ka && a == absorbing) || (kb && b == absorbing)) {
        *out = absorbing;
        return true;
    }
    *out = absorbing ? (a | b) : (a & b);
    return ka && kb;
}

static bool binary(const Folder *f, const Rvalue *rv, uint64_t *out) {
    const Ty *ty = int_type(f, rv->a.type);
    if (!ty && (rv->op == OP_AND || rv->op == OP_OR)) {
        return logic(f, rv, out);
    }
    uint64_t a, b;
    bool result;
    if (!known(f, rv->a, &a) || !known(f, rv->b, &b) ||
        !compare(rv->op, a, b, ty && is_signed(ty), &result)) {
        return false;
    }
    *out = result;
    return true;
}

static bool eval(const Folder *f, const Rvalue *rv, TypeId dest, uint64_t *out) {
    uint64_t a;
    const Ty *from = int_type(f, rv->a.type), *to = int_type(f, rv->to);
    switch (rv->kind) {
    case RV_USE:
        return known(f, rv->a, out);
    case RV_UNARY:
        if (rv->op != OP_NOT || int_type(f, dest) || !known(f, rv->a, &a)) {
            return false;
        }
        *out = !a;
        return true;
    case RV_BINARY:
        return binary(f, rv, out);
    case RV_CAST:
        if (!from || !to || !known(f, rv->a, &a) || cast_fails(from, to, a)) {
            return false;
        }
        *out = normalize(f, rv->to, a);
        return true;
    case RV_CAST_FAILS:
        if (!from || !to || !known(f, rv->a, &a)) {
            return false;
        }
        *out = cast_fails(from, to, a);
        return true;
    default:
        return false;
    }
}

static void forget(Folder *f, LocalId local) {
    f->stamp[local.id] = 0;
}

static void step(Folder *f, const AirStmt *s) {
    if (s->kind == AIR_CALL) {
        if (s->has_dest) {
            forget(f, s->dest);
        }
        return;
    }
    if (s->as.rvalue.kind == RV_CHECKED) {
        forget(f, s->as.rvalue.flag);
    }
    uint64_t v;
    TypeId type = f->body->locals.data[s->dest.id].type;
    if (s->dest_path.len || s->dest_path.deref || f->escaped[s->dest.id] ||
        f->body->locals.data[s->dest.id].name.id || !eval(f, &s->as.rvalue, type, &v)) {
        forget(f, s->dest);
        return;
    }
    f->stamp[s->dest.id] = f->block + 1;
    f->value[s->dest.id] = v;
}

// Calls `visit` on each operand `s` reads.
typedef void (*Visit)(void *context, Operand o);

static void stmt_operands(const AirStmt *s, Visit visit, void *context) {
    if (s->dest_path.deref) {   // writes through the address the local holds
        visit(context, (Operand){.kind = OPERAND_COPY, .as.local = s->dest});
    }
    if (s->kind == AIR_CALL) {
        for (uint32_t i = 0; i < s->as.call.arg_count; i++) {
            visit(context, s->as.call.args[i]);
        }
        return;
    }
    const Rvalue *rv = &s->as.rvalue;
    visit(context, rv->a);
    visit(context, rv->b);
    for (uint32_t i = 0; i < rv->count; i++) {
        visit(context, rv->items[i]);
    }
}

static void term_operands(const Terminator *t, Visit visit, void *context) {
    switch (t->kind) {
    case TERM_BRANCH: case TERM_PANIC:
        visit(context, t->cond);
        break;
    case TERM_ASSERT:
        visit(context, t->cond);
        if (t->panic == PANIC_BOUNDS) {
            visit(context, t->index);
            visit(context, t->len);
        }
        break;
    default:
        break;
    }
}

static void mark_escaped(void *context, Operand o) {
    if (o.kind == OPERAND_ADDR) {
        ((bool *)context)[o.as.local.id] = true;
    }
}

typedef struct {
    uint32_t *reads;
    int32_t delta;
} Counter;

static void count_read(void *context, Operand o) {
    Counter *c = context;
    if (o.kind == OPERAND_COPY || o.kind == OPERAND_ADDR) {
        c->reads[o.as.local.id] += (uint32_t)c->delta;
    }
}

static bool removable(const AirBody *body, const AirStmt *s, const uint32_t *reads) {
    if (s->kind != AIR_ASSIGN || s->dest_path.len || s->dest_path.deref) {
        return false;
    }
    uint32_t id = s->dest.id;
    if (id <= body->param_count || body->locals.data[id].name.id || reads[id]) {
        return false;
    }
    switch (s->as.rvalue.kind) {
    case RV_USE: case RV_BINARY: case RV_UNARY: case RV_CAST: case RV_CAST_FAILS:
        return true;
    default:
        return false;
    }
}

// One backward sweep over `block`; true if it removed a statement.
static bool remove_in_block(const AirBody *body, AirBlock *block, Counter *c) {
    size_t out = block->stmts.len;
    for (size_t i = block->stmts.len; i-- > 0;) {
        AirStmt *s = &block->stmts.data[i];
        if (removable(body, s, c->reads)) {
            stmt_operands(s, count_read, c);
        } else {
            block->stmts.data[--out] = *s;
        }
    }
    size_t kept = block->stmts.len - out;
    for (size_t i = 0; i < kept && out; i++) {
        block->stmts.data[i] = block->stmts.data[out + i];
    }
    bool removed = out != 0;
    block->stmts.len = kept;
    return removed;
}

// Removes the assignments to temporaries that nothing reads, until none is
// left: a removal can leave the temporaries it read unread.
static void remove_dead(AirBody *body) {
    Counter c = {.reads = calloc(body->locals.len, sizeof *c.reads), .delta = 1};
    for (size_t b = 0; b < body->blocks.len; b++) {
        AirBlock *block = &body->blocks.data[b];
        for (size_t i = 0; i < block->stmts.len; i++) {
            stmt_operands(&block->stmts.data[i], count_read, &c);
        }
        term_operands(&block->term, count_read, &c);
    }
    c.delta = -1;
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t b = body->blocks.len; b-- > 0;) {
            changed = remove_in_block(body, &body->blocks.data[b], &c) || changed;
        }
    }
    free(c.reads);
}

void air_fold_checks(AirBody *body, const TypeTable *types) {
    size_t n = body->locals.len;
    Folder f = {.body = body, .types = types, .stamp = calloc(n, sizeof *f.stamp),
                .value = calloc(n, sizeof *f.value), .escaped = calloc(n, sizeof *f.escaped)};
    bool folded = false;
    for (size_t b = 0; b < body->blocks.len; b++) {
        AirBlock *block = &body->blocks.data[b];
        for (size_t i = 0; i < block->stmts.len; i++) {
            stmt_operands(&block->stmts.data[i], mark_escaped, f.escaped);
        }
        term_operands(&block->term, mark_escaped, f.escaped);
    }
    for (size_t i = 0; i < n; i++) {
        f.escaped[i] = f.escaped[i] || body->locals.data[i].by_ref;
    }
    for (size_t b = 0; b < body->blocks.len; b++) {
        AirBlock *block = &body->blocks.data[b];
        f.block = (uint32_t)b;
        for (size_t i = 0; i < block->stmts.len; i++) {
            step(&f, &block->stmts.data[i]);
        }
        uint64_t v;
        if (block->term.kind == TERM_ASSERT && known(&f, block->term.cond, &v) && !v) {
            block->term = (Terminator){.kind = TERM_GOTO, .target = block->term.target};
            folded = true;
        }
    }
    free(f.stamp);
    free(f.value);
    free(f.escaped);
    if (folded) {
        remove_dead(body);
    }
}
