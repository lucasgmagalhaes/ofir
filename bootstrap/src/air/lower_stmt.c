// Lowering of statements and blocks.

#include "air/lower_internal.h"

static void assign_operand(Lowerer *l, LocalId dest, Operand value) {
    if (value.kind != OPERAND_UNIT) {
        emit_assign(l, dest, (Rvalue){.kind = RV_USE, .a = value});
    }
}

static void lower_let(Lowerer *l, Stmt *s) {
    Operand value = lower_expr(l, s->as.let.value);
    Pattern *pattern = s->as.let.pattern;
    if (pattern->kind == PAT_BINDING) {
        assign_operand(l, air_local_of(pattern->as.binding.local), value);
    } else if (pattern->kind != PAT_WILDCARD && value.kind != OPERAND_UNIT) {
        // On a mismatch: the `else` block, which cannot fall through; without
        // `else` the pattern always matches (checked).
        // A consumed value: what the bindings leave ends with the statement,
        // all of it on a mismatch.
        Expr *init = s->as.let.value;
        bool consumed = init->consumed || init->moves;
        Place place = {.local = to_local(l, value)};
        BlockId fail = air_new_block(l->body);
        lower_pattern(l, pattern, place, fail);
        BlockId next = air_new_block(l->body);
        emit_goto(l, next);
        switch_to(l, fail);
        if (s->as.let.else_) {
            if (consumed) {
                emit_drop(l, place, concrete_type(l, pattern->type));
            }
            lower_block(l, s->as.let.else_, false);
        }
        terminate(l, (Terminator){.kind = TERM_UNREACHABLE});
        switch_to(l, next);
        if (consumed) {
            keep_unbound(l, pattern, place);
        }
    }
    own_bindings(l, pattern);
}

// Whether `target` is written through a raw pointer: no old value is
// dropped, as by `ptr.write` (the old value may be uninitialized or moved).
static bool through_raw_pointer(Lowerer *l, const Expr *target) {
    while (target->kind == EXPR_FIELD || target->kind == EXPR_TUPLE_FIELD) {
        target = target->kind == EXPR_FIELD ? target->as.field.base : target->as.tuple_field.base;
    }
    if (target->kind != EXPR_UNARY || target->as.unary.op != OP_DEREF) {
        return false;
    }
    const Expr *operand = target->as.unary.operand;
    bool box = operand->kind == EXPR_FIELD &&
               type_get(l->types, operand->as.field.base->type)->kind == TY_ADT &&
               type_get(l->types, operand->as.field.base->type)->data == l->sema->box.id;
    return type_get(l->types, operand->type)->kind == TY_PTR && !box;
}

// `place = value` drops the value the place held (spec 5.2): through the
// drop flag for a local, at once for a field, an element or `*r`. The new
// value is read first.
static Operand drop_old_value(Lowerer *l, const Expr *target, Place place, Operand value) {
    bool whole = place.path.len == 0 && !place.path.deref;
    TypeId type = concrete_type(l, target->type);
    if (through_raw_pointer(l, target) || !needs_drop(l, type)) {
        return value;
    }
    value = op_copy(to_local(l, value), value.type);
    if (assign_part(l, target, place, type)) {
        return value;   // a part with its own drop flag
    }
    if (target->moves) {
        return value;   // the old value may have been moved out
    }
    if (whole) {
        drop_before_assign(l, place.local);
    } else {
        emit_drop(l, place, type);
    }
    return value;
}

// The right side runs before the place (spec 4.2): a value read from a
// place is copied out first, so that the place's code cannot change it.
static void lower_assign(Lowerer *l, Stmt *s) {
    Expr *target = s->as.assign.target;
    Operand value = lower_expr(l, s->as.assign.value);
    if (value.kind == OPERAND_COPY && (value.path.len > 0 || value.path.deref)) {
        value = op_copy(to_local(l, value), value.type);
    }
    Place place = lower_place(l, target);
    if (s->as.assign.op == OP_ADD && target->type.id == TID_STR) {   // s += t appends in place
        Operand part = {.kind = OPERAND_ADDR, .type = value.type, .as.local = to_local(l, value)};
        Operand args[] = {place_operand(place, OPERAND_ADDR, target->type), part};
        emit_runtime(l, RT_STR_PUSH_STR, args, 2, NULL);
        return;
    }
    if (s->as.assign.op != OP_ASSIGN) {   // x op= y is x = x op y
        Operand current = place_operand(place, OPERAND_COPY, target->type);
        Arith x = {.op = s->as.assign.op, .type = target->type, .a = current, .b = value,
                   .span = s->span};
        value = lower_binary_op(l, x);
    }
    if (value.kind == OPERAND_UNIT) {
        return;
    }
    value = drop_old_value(l, target, place, value);
    emit_store(l, place, value);
    if (place.path.len == 0 && !place.path.deref) {
        assign_done(l, place.local);
    }
}

// The body of a loop; `latch` is where `continue` goes.
void lower_loop_body(Lowerer *l, Block *body, BlockId latch, BlockId exit) {
    VEC_PUSH(l->loops, ((Loop){.break_to = exit, .continue_to = latch,
                              .scopes = (uint32_t)l->scopes.len,
                              .temp_scopes = (uint32_t)l->temp_scopes.len}));
    lower_block(l, body, false);
    l->loops.len--;
    emit_goto(l, latch);
}

// `for { }` and `for cond { }`.
static void lower_while(Lowerer *l, Stmt *s) {
    BlockId header = air_new_block(l->body), body = air_new_block(l->body);
    BlockId exit = air_new_block(l->body);
    emit_goto(l, header);
    switch_to(l, header);
    if (s->as.for_.kind == FOR_COND) {
        temp_scope_enter(l);   // evaluated at each iteration
        Operand cond = lower_expr(l, s->as.for_.expr);
        cond = op_copy(to_local(l, cond), cond.type);
        temp_scope_leave(l);
        terminate(l, (Terminator){.kind = TERM_BRANCH, .cond = cond, .target = body,
                                  .other = exit});
    } else {
        emit_goto(l, body);
    }
    switch_to(l, body);
    lower_loop_body(l, s->as.for_.body, header, exit);
    switch_to(l, exit);
}

// `for i in lo..hi`: a hidden counter, copied into `i` at each iteration so
// that `for mut i` cannot change the iteration. The increment cannot
// overflow: it happens only while the counter is below `hi`.
static void lower_for_range(Lowerer *l, Stmt *s) {
    Expr *range = s->as.for_.expr;
    TypeId type = range->type;
    // Both bounds are evaluated once, before the loop.
    LocalId counter = emit_temp(l, type, (Rvalue){.kind = RV_USE,
                                                  .a = lower_expr(l, range->as.range.lo)});
    LocalId end = emit_temp(l, type, (Rvalue){.kind = RV_USE,
                                              .a = lower_expr(l, range->as.range.hi)});
    Operand hi = op_copy(end, type);
    Operand current = op_copy(counter, type);
    BlockId header = air_new_block(l->body), body = air_new_block(l->body);
    BlockId latch = air_new_block(l->body), exit = air_new_block(l->body);
    bool inclusive = range->as.range.inclusive;
    emit_goto(l, header);
    switch_to(l, header);   // inclusive: enter if lo <= hi; exclusive: loop while i < hi
    Rvalue test = {.kind = RV_BINARY, .op = inclusive ? OP_LE : OP_LT, .a = current, .b = hi};
    Operand go = op_copy(emit_temp(l, type_id(TID_BOOL), test), type_id(TID_BOOL));
    terminate(l, (Terminator){.kind = TERM_BRANCH, .cond = go, .target = body, .other = exit});
    switch_to(l, body);
    emit_assign(l, air_local_of(s->as.for_.first_local), (Rvalue){.kind = RV_USE, .a = current});
    lower_loop_body(l, s->as.for_.body, latch, exit);
    switch_to(l, latch);
    BlockId step = air_new_block(l->body);
    if (inclusive) {   // stop at hi itself, before an increment that could overflow
        Rvalue last = {.kind = RV_BINARY, .op = OP_EQ, .a = current, .b = hi};
        Operand done = op_copy(emit_temp(l, type_id(TID_BOOL), last), type_id(TID_BOOL));
        terminate(l, (Terminator){.kind = TERM_BRANCH, .cond = done, .target = exit,
                                  .other = step});
    } else {
        emit_goto(l, step);
    }
    switch_to(l, step);
    emit_assign(l, counter, (Rvalue){.kind = RV_BINARY, .op = OP_ADD, .a = current,
                                     .b = op_int(1, type)});
    emit_goto(l, inclusive ? body : header);
    switch_to(l, exit);
}

static void lower_jump(Lowerer *l, Stmt *s) {
    Loop loop = VEC_LAST(l->loops);
    drop_temps(l, loop.temp_scopes);
    drop_scopes(l, loop.scopes);
    emit_goto(l, s->kind == STMT_BREAK ? loop.break_to : loop.continue_to);
}

static void lower_return(Lowerer *l, Stmt *s) {
    if (s->as.expr) {
        assign_operand(l, (LocalId){0}, lower_expr(l, s->as.expr));
    }
    drop_temps(l, 0);
    drop_scopes(l, 0);
    terminate(l, (Terminator){.kind = TERM_RETURN});
}

static void lower_stmt(Lowerer *l, Stmt *s) {
    switch (s->kind) {
    case STMT_LET: lower_let(l, s); break;
    case STMT_VAR:
        if (s->as.var.value) {
            assign_operand(l, air_local_of(s->as.var.local), lower_expr(l, s->as.var.value));
        }
        own_local(l, air_local_of(s->as.var.local), s->as.var.value != NULL);
        break;
    case STMT_ASSIGN: lower_assign(l, s); break;
    case STMT_FOR:
        if (s->as.for_.kind == FOR_IN && s->as.for_.expr->kind != EXPR_RANGE &&
            is_iterator(l, s->as.for_.expr->type)) {
            lower_for_iter(l, s);
        } else if (s->as.for_.kind == FOR_IN && s->as.for_.expr->kind != EXPR_RANGE) {
            lower_for_list(l, s);
        } else if (s->as.for_.kind == FOR_IN) {
            lower_for_range(l, s);
        } else {
            lower_while(l, s);
        }
        break;
    case STMT_RETURN: lower_return(l, s); break;
    case STMT_BREAK: case STMT_CONTINUE: lower_jump(l, s); break;
    case STMT_EXPR: lower_expr(l, s->as.expr); break;
    }
}

// The last expression of a block or a `match` arm, whose value is the
// block's: it is not a temporary of the block, but its own temporaries
// are dropped before the block ends. Its value is read before that.
Operand lower_tail(Lowerer *l, Expr *e) {
    temp_scope_enter(l);
    l->forward = true;
    Operand result = lower_expr(l, e);
    if (result.kind == OPERAND_COPY) {
        result = op_copy(emit_temp(l, result.type, (Rvalue){.kind = RV_USE, .a = result}),
                         result.type);
    }
    temp_scope_leave(l);
    return result;
}

// As a value, a block is worth its last expression statement.
Operand lower_block(Lowerer *l, Block *block, bool as_value) {
    StmtList stmts = block->stmts;
    Operand result = op_unit();
    drop_scope_enter(l);
    for (uint32_t i = 0; i < stmts.len; i++) {
        Stmt *s = stmts.items[i];
        if (as_value && i + 1 == stmts.len && s->kind == STMT_EXPR) {
            result = lower_tail(l, s->as.expr);
            break;
        }
        temp_scope_enter(l);   // a statement's temporaries end with it
        lower_stmt(l, s);
        temp_scope_leave(l);
    }
    drop_scope_leave(l);
    return result;
}
