// Move analysis (spec 5.1, roadmap 3.1): states, statements and loops.
// Expressions and places are in moves_expr.c.

#include "sema/moves_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// States

State state_new(Moves *m) {
    return (State){.moved = ARENA_ARRAY(m->arena, uint64_t, m->words),
                   .uninit = ARENA_ARRAY(m->arena, uint64_t, m->words)};
}

State state_copy(Moves *m, const State *s) {
    State out = state_new(m);
    out.dead = s->dead;
    memcpy(out.moved, s->moved, m->words * sizeof(uint64_t));
    memcpy(out.uninit, s->uninit, m->words * sizeof(uint64_t));
    if (s->part_count > 0) {
        out.parts = ARENA_ARRAY(m->arena, Part, s->part_count);
        memcpy(out.parts, s->parts, s->part_count * sizeof(Part));
        out.part_count = out.part_cap = s->part_count;
    }
    return out;
}

static bool same_part(const Part *a, const Part *b) {
    return a->local == b->local && a->len == b->len &&
           memcmp(a->fields, b->fields, a->len * sizeof(uint32_t)) == 0;
}

static void add_part(Moves *m, State *s, const Part *part) {
    for (uint32_t i = 0; i < s->part_count; i++) {
        if (same_part(&s->parts[i], part)) {
            return;
        }
    }
    if (s->part_count == s->part_cap) {
        uint32_t cap = s->part_cap ? s->part_cap * 2 : 4;
        Part *parts = ARENA_ARRAY(m->arena, Part, cap);
        if (s->part_count) {
            memcpy(parts, s->parts, s->part_count * sizeof(Part));
        }
        s->parts = parts;
        s->part_cap = cap;
    }
    s->parts[s->part_count++] = *part;
}

void state_join(Moves *m, State *into, const State *other) {
    if (other->dead) {
        return;
    }
    if (into->dead) {
        *into = state_copy(m, other);
        return;
    }
    for (uint32_t w = 0; w < m->words; w++) {
        into->moved[w] |= other->moved[w];
        into->uninit[w] |= other->uninit[w];
    }
    for (uint32_t i = 0; i < other->part_count; i++) {
        add_part(m, into, &other->parts[i]);
    }
}

static bool state_equal(Moves *m, const State *a, const State *b) {
    if (a->dead != b->dead || a->part_count != b->part_count ||
        memcmp(a->moved, b->moved, m->words * sizeof(uint64_t)) != 0 ||
        memcmp(a->uninit, b->uninit, m->words * sizeof(uint64_t)) != 0) {
        return false;
    }
    for (uint32_t i = 0; i < a->part_count; i++) {
        if (!same_part(&a->parts[i], &b->parts[i])) {
            // GCOVR_EXCL_START: a join appends parts: the same count is the same parts
            return false;
            // GCOVR_EXCL_STOP
        }
    }
    return true;
}

// One path is a prefix of the other: using one uses the other.
static bool overlaps(const Part *a, const Part *b) {
    uint32_t len = a->len < b->len ? a->len : b->len;
    return a->local == b->local && memcmp(a->fields, b->fields, len * sizeof(uint32_t)) == 0;
}

bool moved_part(const State *s, const Part *part) {
    if (bit(s->moved, part->local)) {
        return true;
    }
    for (uint32_t i = 0; i < s->part_count; i++) {
        if (overlaps(&s->parts[i], part)) {
            return true;
        }
    }
    return false;
}

void move_part(Moves *m, State *s, const Part *part) {
    if (part->len == 0) {
        set_bit(s->moved, part->local, true);
        return;
    }
    add_part(m, s, part);
    for (size_t i = 0; i < m->all_parts.len; i++) {
        if (same_part(&m->all_parts.data[i], part)) {
            return;
        }
    }
    VEC_PUSH(m->all_parts, *part);
}

// An assignment to `part` makes it, and everything inside it, a value again.
void init_part(Moves *m, State *s, const Part *part) {
    (void)m;
    if (part->len == 0) {
        set_bit(s->moved, part->local, false);
        set_bit(s->uninit, part->local, false);
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < s->part_count; i++) {
        const Part *p = &s->parts[i];
        bool inside = p->local == part->local && p->len >= part->len &&
                      memcmp(p->fields, part->fields, part->len * sizeof(uint32_t)) == 0;
        if (!inside) {
            s->parts[kept++] = *p;
        }
    }
    s->part_count = kept;
}

void move_error(Moves *m, const char *code, Span span, const char *format, ...) {
    if (!m->report) {
        return;
    }
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    diag_error(&m->c->sema->session->diags, code, span, "%s", message);
}

// ---------------------------------------------------------------------------
// Statements

static StrView local_name(Moves *m, uint32_t local) {
    return symbol_name(&m->c->sema->session->names, m->fn->locals.items[local].name);
}

// A `mut` parameter may be moved out of, but it must hold a value again
// when the function returns: the caller still owns it (spec 5.3).
static void check_mut_params(Moves *m, const State *s, Span span) {
    if (s->dead) {
        return;
    }
    for (uint32_t i = 0; i <= m->fn->params.len; i++) {
        bool is_self = i == m->fn->params.len;
        if (is_self && !(m->fn->has_self && m->fn->self_mode == MODE_MUT)) {
            continue;
        }
        if (!is_self && m->fn->params.items[i].mode != MODE_MUT) {
            continue;
        }
        uint32_t local = is_self ? 0 : m->fn->params.items[i].local.id;
        if (moved_part(s, &(Part){.local = local})) {
            move_error(m, "E0602", span, "`" SV_FMT "` is moved out of and not given a new "
                       "value before the function returns", SV_ARG(local_name(m, local)));
        }
    }
}

static void loop_body(Moves *m, Stmt *s, State *st);

static void moves_assign(Moves *m, Stmt *s, State *st) {
    // `x = v` moves `v` into `x`; `x op= v` only reads it (`s += t` appends).
    moves_expr(m, s->as.assign.value, st, s->as.assign.op == OP_ASSIGN ? USE_MOVE : USE_READ);
    Expr *target = s->as.assign.target;
    if (s->as.assign.op != OP_ASSIGN) {   // `x += 1` reads `x` first
        moves_expr(m, target, st, USE_READ);
        return;
    }
    moves_expr(m, target, st, USE_ASSIGN);
}

static void moves_let(Moves *m, Stmt *s, State *st) {
    // `const _ = value` keeps nothing: the value is a temporary, dropped at once.
    bool keeps = s->as.let.pattern->kind != PAT_WILDCARD;
    moves_expr(m, s->as.let.value, st, keeps ? USE_MOVE : USE_READ);
    if (s->as.let.else_) {
        State other = state_copy(m, st);
        moves_block(m, s->as.let.else_, &other, USE_READ);   // never falls through
    }
    moves_pattern_init(m, s->as.let.pattern, st, false);
}

static void moves_stmt(Moves *m, Stmt *s, State *st) {
    switch (s->kind) {
    case STMT_LET: moves_let(m, s, st); break;
    case STMT_VAR:
        if (s->as.var.value) {
            moves_expr(m, s->as.var.value, st, USE_MOVE);
        }
        set_bit(st->uninit, s->as.var.local.id, s->as.var.value == NULL);
        set_bit(st->moved, s->as.var.local.id, false);
        break;
    case STMT_ASSIGN: moves_assign(m, s, st); break;
    case STMT_FOR: loop_body(m, s, st); break;
    case STMT_RETURN:
        if (s->as.expr) {
            moves_expr(m, s->as.expr, st, USE_MOVE);
        }
        check_mut_params(m, st, s->span);
        st->dead = true;
        break;
    case STMT_BREAK: case STMT_CONTINUE:
        if (m->loops.len > 0) {   // else E0309 was reported
            LoopFrame *frame = &VEC_LAST(m->loops);
            state_join(m, s->kind == STMT_BREAK ? &frame->breaks : &frame->continues, st);
        }
        st->dead = true;
        break;
    case STMT_EXPR:
        moves_expr(m, s->as.expr, st, USE_READ);
        break;
    }
}

void moves_block(Moves *m, Block *block, State *s, Use tail) {
    for (uint32_t i = 0; i < block->stmts.len; i++) {
        Stmt *stmt = block->stmts.items[i];
        bool last = i + 1 == block->stmts.len && stmt->kind == STMT_EXPR;
        if (last) {
            moves_expr(m, stmt->as.expr, s, tail);
        } else {
            moves_stmt(m, stmt, s);
        }
    }
}

// ---------------------------------------------------------------------------
// Loops

// One iteration from `entry`: the condition or the iterated value, the
// bindings, the body. Returns the state at the loop's exit.
static State iteration(Moves *m, Stmt *s, const State *entry) {
    LoopFrame *frame = &VEC_LAST(m->loops);
    frame->breaks = (State){.dead = true};
    frame->continues = (State){.dead = true};
    State st = state_copy(m, entry);
    State exit = {.dead = true};
    if (s->as.for_.kind == FOR_COND) {
        moves_expr(m, s->as.for_.expr, &st, USE_READ);
        exit = state_copy(m, &st);   // the condition was false
    } else if (s->as.for_.kind == FOR_IN) {
        TypeId item;
        if (s->as.for_.expr->kind == EXPR_RANGE ||
            iterator_item(m->c, s->as.for_.expr->type, &item)) {   // the variable owns
            exit = state_copy(m, &st);
            init_part(m, &st, &(Part){.local = s->as.for_.first_local.id});   // a new item
        } else {   // the variables borrow the elements
            set_bit(m->borrowed, s->as.for_.first_local.id, true);
            if (s->as.for_.second.id != 0) {
                set_bit(m->borrowed, s->as.for_.second_local.id, true);
            }
            exit = state_copy(m, &st);
        }
    }
    moves_block(m, s->as.for_.body, &st, USE_READ);
    state_join(m, &frame->continues, &st);
    state_join(m, &exit, &frame->breaks);
    return exit;
}

// The state at the start of an iteration is the entry state joined with
// the states at the end of every iteration. The first walk reports; when
// an iteration changes that state (it moves or initializes something),
// the loop is walked without reporting until the state is stable, then
// once more to report. Errors reported twice are rendered once.
static void loop_body(Moves *m, Stmt *s, State *st) {
    if (s->as.for_.kind == FOR_IN) {
        moves_expr(m, s->as.for_.expr, st, USE_READ);   // evaluated once, borrowed
    }
    VEC_PUSH(m->loops, ((LoopFrame){0}));
    State entry = state_copy(m, st);
    State exit = iteration(m, s, &entry);
    State next = state_copy(m, &entry);
    state_join(m, &next, &VEC_LAST(m->loops).continues);
    if (!state_equal(m, &next, &entry)) {
        bool report = m->report;
        m->report = false;
        for (int round = 0; round < 16 && !state_equal(m, &next, &entry); round++) {
            entry = next;
            iteration(m, s, &entry);
            next = state_copy(m, &entry);
            state_join(m, &next, &VEC_LAST(m->loops).continues);
        }
        m->report = report;
        exit = iteration(m, s, &entry);
    }
    *st = exit;
    m->loops.len--;
}

// ---------------------------------------------------------------------------
// Whether a function needs the analysis (Checker.owned_values)

void note_owned(Checker *c, TypeId type) {
    TyKind kind = type_get(c->types, type)->kind;
    if (kind > TY_FLOAT && kind != TY_REF && kind != TY_PTR) {   // not trivially Copy
        c->owned_values = true;
    }
}

// `Wrap(x)`, `Some(x)`, `Shape.Circle(r)`: a constructor or a variant.
static bool builds_value(Checker *c, const Expr *call) {
    const Expr *callee = call->as.call.callee;
    if (callee->kind == EXPR_FIELD) {
        return callee->as.field.res.kind == RES_VARIANT;
    }
    if (callee->kind != EXPR_IDENT && callee->kind != EXPR_GENERIC) {
        return false;
    }
    Res res = (callee->kind == EXPR_GENERIC ? callee->as.generic.res : callee->as.ident.res);
    if (res.kind != RES_DEF) {
        return false;
    }
    DefKind kind = sema_def(c->sema, (DefId){res.index})->kind;
    return kind == DEF_STRUCT || kind == DEF_VARIANT;
}

// An aggregate moves its parts in; an `if`, a `match` or a block, its tails.
void note_aggregate(Checker *c, const Expr *e) {
    bool aggregate = e->kind == EXPR_STRUCT || e->kind == EXPR_TUPLE || e->kind == EXPR_ARRAY ||
                     e->kind == EXPR_ARRAY_REPEAT || e->kind == EXPR_VARIANT ||
                     e->kind == EXPR_TRY || e->kind == EXPR_IF || e->kind == EXPR_MATCH ||
                     e->kind == EXPR_BLOCK || e->kind == EXPR_UNSAFE ||
                     (e->kind == EXPR_BINARY && e->as.binary.op == OP_COALESCE) ||
                     (e->kind == EXPR_CALL && builds_value(c, e));
    if (aggregate) {
        note_owned(c, e->type);
    }
}

// What a function's signature may move: its result and its parameters.
void note_signature(Checker *c, const FnDecl *fn, const Ty *sig, TypeId self_type) {
    c->owned_values = false;
    note_owned(c, sig->inner);
    for (uint32_t i = 0; i < sig->arg_count; i++) {
        note_owned(c, sig->args[i]);
    }
    if (fn->has_self) {
        note_owned(c, self_type);
    }
}

void check_moves(Checker *c, FnDecl *fn) {
    if (!fn->body) {
        return;   // GCOVR_EXCL_LINE: only a function with a body is analyzed
    }
    Moves m = {.c = c, .fn = fn, .report = true, .arena = &c->scratch};
    arena_reset(m.arena);
    m.words = (fn->locals.len + 63) / 64 + 1;
    m.borrowed = ARENA_ARRAY(m.arena, uint64_t, m.words);
    if (fn->has_self && fn->self_mode == MODE_BORROW) {
        set_bit(m.borrowed, 0, true);
    }
    for (uint32_t i = 0; i < fn->params.len; i++) {
        if (fn->params.items[i].mode == MODE_BORROW) {
            set_bit(m.borrowed, fn->params.items[i].local.id, true);
        }
    }
    State s = state_new(&m);
    moves_block(&m, fn->body, &s, USE_READ);
    check_mut_params(&m, &s, fn->span);
    for (uint32_t i = 0; i < fn->locals.len; i++) {   // what the drops need
        fn->locals.items[i].borrows = bit(m.borrowed, i);
    }
    fn->moved_part_count = (uint32_t)m.all_parts.len;
    fn->moved_parts = ARENA_ARRAY(&c->sema->session->arena, MovedPart, m.all_parts.len + 1);
    if (m.all_parts.len > 0) {
        memcpy(fn->moved_parts, m.all_parts.data, m.all_parts.len * sizeof(MovedPart));
    }
    VEC_FREE(m.all_parts);
    VEC_FREE(m.loops);
}
