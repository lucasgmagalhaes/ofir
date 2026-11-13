// Lowering: functions and the helpers that build blocks and statements.

#include "air/lower_internal.h"

LocalId new_temp(Lowerer *l, TypeId type) {
    return air_new_local(l->body, type, (Symbol){0}, (Span){0});
}

void emit_assign(Lowerer *l, LocalId dest, Rvalue rvalue) {
    AirStmt s = {.kind = AIR_ASSIGN, .dest = dest, .has_dest = true};
    s.as.rvalue = rvalue;
    air_push(l->body, l->block, s);
}

LocalId emit_temp(Lowerer *l, TypeId type, Rvalue rvalue) {
    LocalId temp = new_temp(l, type);
    emit_assign(l, temp, rvalue);
    return temp;
}

// The function of std/core/str.of that a runtime function is lowered to,
// or NULL.
static const char *std_runtime(RuntimeFn fn) {
    switch (fn) {
    case RT_STR_PUSH_STR: return "strPushStr";
    case RT_STR_PUSH_BOOL: return "strPushBool";
    case RT_STR_PUSH_INT: return "strPushInt";
    case RT_STR_PUSH_UINT: return "strPushUint";
    default: return NULL;
    }
}

void emit_runtime(Lowerer *l, RuntimeFn fn, const Operand *args, uint32_t count, LocalId *dest) {
    AirStmt s = {.kind = AIR_CALL, .has_dest = dest != NULL};
    if (dest) {
        s.dest = *dest;
    }
    s.as.call.runtime = true;
    s.as.call.rt = fn;
    s.as.call.arg_count = count;
    s.as.call.args = ARENA_ARRAY(&l->sema->session->arena, Operand, count + 1);
    for (uint32_t i = 0; i < count; i++) {
        s.as.call.args[i] = args[i];
    }
    const char *name = std_runtime(fn);
    if (name) {
        s.as.call.runtime = false;
        s.as.call.fn = std_fn(l, "std.core.str", name)->def;
        s.as.call.targs = type_id(TID_UNIT);
        if (fn == RT_STR_PUSH_STR) {   // the part is passed by value
            s.as.call.args[1].kind = OPERAND_COPY;
        }
    }
    air_push(l->body, l->block, s);
}

// Continues in `block`. The block left behind was opened by `terminate` and
// nothing jumps to it: it ends as unreachable.
void switch_to(Lowerer *l, BlockId block) {
    AirBlock *current = air_block(l->body, l->block);
    if (current->term.kind == TERM_NONE) {
        current->term.kind = TERM_UNREACHABLE;
    }
    l->block = block;
}

void terminate(Lowerer *l, Terminator term) {
    air_terminate(l->body, l->block, term);
    l->block = air_new_block(l->body);   // unreachable until something jumps here
}

void emit_goto(Lowerer *l, BlockId target) {
    terminate(l, (Terminator){.kind = TERM_GOTO, .target = target});
}

void emit_assert(Lowerer *l, Operand fails, PanicKind panic, Span span) {
    BlockId next = air_new_block(l->body);
    air_terminate(l->body, l->block, (Terminator){.kind = TERM_ASSERT, .cond = fails,
                                                  .target = next, .panic = panic, .span = span});
    l->block = next;
}

LocalId to_local(Lowerer *l, Operand operand) {
    if (operand.kind == OPERAND_COPY && operand.path.len == 0 && !operand.path.deref) {
        return operand.as.local;
    }
    return emit_temp(l, operand.type, (Rvalue){.kind = RV_USE, .a = operand});
}

// The function's scope owns its `move` parameters (spec 5.3).
static void own_params(Lowerer *l, FnDecl *fn) {
    l->flag_count = fn->locals.len + 1;
    l->flags = ARENA_ARRAY(&l->sema->session->arena, LocalId, l->flag_count + 1);
    l->fn_locals = fn->locals.items;
    l->fn = fn;
    l->part_flags.len = 0;
    l->owned.len = 0;
    l->scopes.len = 0;
    l->temps.len = 0;
    l->temp_scopes.len = 0;
    l->forward = false;
    drop_scope_enter(l);
    if (fn->has_self && fn->self_mode == MODE_MOVE) {
        own_local(l, (LocalId){1}, true);
    }
    for (uint32_t i = 0; i < fn->params.len; i++) {
        if (fn->params.items[i].mode == MODE_MOVE) {
            own_local(l, air_local_of(fn->params.items[i].local), true);
        }
    }
}

static void lower_fn(Lowerer *l, Instance inst, BodySink sink, void *context) {
    FnDecl *fn = inst.fn;
    sema_check_late(l->sema, fn);   // a std body is checked when it is reached
    sema_moves_late(l->sema, fn);   // what the drops need
    Def *def = sema_def(l->sema, fn->def);
    Ty sig = *type_get(l->types, def->type);
    AirBody body = {.def = fn->def, .targs = inst.targs, .name = fn->name};
    body.is_main = !l->sema->testing && l->sema->current == 0 && def->item &&
                   def->item->kind == ITEM_FN &&
                   sv_eq(symbol_name(&l->sema->session->names, fn->name), SV("main"));
    body.param_count = fn->params.len + (fn->has_self ? 1 : 0);
    l->body = &body;
    air_new_local(&body, sig.inner, (Symbol){0}, fn->span);   // the return value
    for (uint32_t i = 0; i < fn->locals.len; i++) {
        const Local *local = &fn->locals.items[i];
        air_new_local(&body, fn->local_types[i], local->name, local->span);
    }
    // `mut self`, `mut` parameters and the source of a returned borrow are
    // passed by address (spec 5.3, 5.6).
    if (fn->has_self && by_address(fn, -1)) {
        body.locals.data[1].by_ref = true;
    }
    for (uint32_t i = 0; i < fn->params.len; i++) {
        if (by_address(fn, (int32_t)i)) {
            body.locals.data[air_local_of(fn->params.items[i].local).id].by_ref = true;
        }
    }
    l->block = air_new_block(&body);
    own_params(l, fn);
    lower_block(l, fn->body, false);
    // Falling off the end returns (); other functions were checked to return.
    TermKind end = sig.inner.id == TID_UNIT ? TERM_RETURN : TERM_UNREACHABLE;
    drop_scope_leave(l);   // the `move` parameters
    air_terminate(&body, l->block, (Terminator){.kind = end});
    mono_body(l, &body);
    sink(context, &body);
    air_body_free(&body);
}

// The root module must declare `fn main()`.
static void check_main(Sema *sema) {
    DefId main_fn = scope_module_in(&sema->scope, 0, intern(&sema->session->names, SV("main")));
    Def *main_def = main_fn.id ? sema_def(sema, main_fn) : NULL;
    if (!main_def || main_def->kind != DEF_FN || !main_def->fn->body || main_def->module != 0) {
        diag_error(&sema->session->diags, "E0203",
                   (Span){.file = sema->program.items[0].module->file},
                   "the program has no `main` function");
    } else if (main_def->fn->params.len > 0 || main_def->fn->ret) {
        diag_error(&sema->session->diags, "E0203", main_def->span,
                   "`main` takes no parameters and returns nothing: `fn main() { ... }`");
    }
}

// The roots: `main` and the `extern "C" fn` functions, which C may call.
// Declarations stage 0 cannot compile yet are reported here.
static void find_roots(Lowerer *l, ItemList items) {
    for (uint32_t i = 0; i < items.len; i++) {
        Item *item = items.items[i];
        if (item->kind == ITEM_FN && item->as.fn->body) {
            bool is_main = !l->sema->testing && l->sema->current == 0 &&
                           sv_eq(symbol_name(&l->sema->session->names, item->name), SV("main"));
            if (is_main || item->as.fn->abi.len > 0) {
                reach(l, item->as.fn, type_id(TID_UNIT));
            }
        }
    }
}

// `main` (or, for `ofir test`, the generated entry point and the tests)
// and the `extern "C" fn` functions of every module.
static void find_all_roots(Lowerer *l, BodySink sink, void *context) {
    Sema *sema = l->sema;
    if (!sema->testing) {
        check_main(sema);
    }
    for (uint32_t m = 0; m < sema->program.len; m++) {
        sema_enter(sema, m);
        find_roots(l, sema->module->items);
        if (sema->testing) {
            find_tests(l, m);
        }
    }
    if (sema->testing) {
        sema_enter(sema, 0);
        lower_test_main(l, sink, context);
    }
}

// Lowers the functions reachable from the roots, in the order they are
// reached, so that the output is deterministic.
void lower_program(Sema *sema, BodySink sink, void *context) {
    Lowerer l = {.sema = sema, .types = &sema->types};
    find_all_roots(&l, sink, context);
    for (size_t next = 0; next < l.pending.len; next++) {
        Instance inst = l.pending.data[next];
        if (!inst.fn) {   // glue
            l.flag_count = 0;
            l.fn = NULL;
            if (inst.glue.id == GLUE_CLONE) {
                lower_clone_glue(&l, inst.targs, sink, context);
            } else {
                lower_glue(&l, inst.targs, sink, context);
            }
            continue;
        }
        sema_enter(sema, sema_def(sema, inst.fn->def)->module);
        lower_fn(&l, inst, sink, context);
    }
    sema_enter(sema, 0);
    VEC_FREE(l.loops);
    VEC_FREE(l.owned);
    VEC_FREE(l.scopes);
    VEC_FREE(l.temps);
    VEC_FREE(l.part_flags);
    VEC_FREE(l.drop_cache);
    VEC_FREE(l.temp_scopes);
    mono_free(&l);
}
