#include "sema/scope.h"

void scope_init(Scope *scope, uint32_t symbol_count, uint32_t module_count) {
    *scope = (Scope){.symbol_count = symbol_count, .table_count = module_count};
    scope->tables = xcalloc(module_count, sizeof(uint32_t *));
    for (uint32_t i = 0; i < module_count; i++) {
        scope->tables[i] = xcalloc(symbol_count, sizeof(uint32_t));
    }
    scope->module = scope->tables[0];
    scope->local = xcalloc(symbol_count, sizeof(uint32_t));
}

void scope_use_module(Scope *scope, uint32_t index) {
    scope->module = scope->tables[index];
}

DefId scope_module_in(const Scope *scope, uint32_t index, Symbol name) {
    return (DefId){name.id < scope->symbol_count ? scope->tables[index][name.id] : 0};
}

void scope_free(Scope *scope) {
    for (uint32_t i = 0; i < scope->table_count; i++) {
        free(scope->tables[i]);
    }
    free(scope->tables);
    free(scope->local);
    VEC_FREE(scope->depth);
    VEC_FREE(scope->saved);
    VEC_FREE(scope->marks);
}

DefId scope_module(const Scope *scope, Symbol name) {
    return (DefId){name.id < scope->symbol_count ? scope->module[name.id] : 0};
}

void scope_set_module(Scope *scope, Symbol name, DefId def) {
    scope->module[name.id] = def.id;
}

void scope_enter(Scope *scope) {
    VEC_PUSH(scope->marks, scope->saved.len);
}

void scope_leave(Scope *scope) {
    size_t mark = VEC_POP(scope->marks);
    while (scope->saved.len > mark) {
        Shadowed s = VEC_POP(scope->saved);
        scope->local[s.name.id] = s.previous;
    }
}

void scope_reset_locals(Scope *scope) {
    if (scope->marks.len != 0) {
        // GCOVR_EXCL_START: a compiler bug: blocks are closed before
        fatal("scope_reset_locals: a block is still open");
        // GCOVR_EXCL_STOP
    }
    scope->depth.len = 0;
}

bool scope_local(const Scope *scope, Symbol name, LocalId *out) {
    uint32_t bound = name.id < scope->symbol_count ? scope->local[name.id] : 0;
    if (bound == 0) {
        return false;
    }
    *out = (LocalId){bound - 1};
    return true;
}

bool scope_in_current_block(const Scope *scope, Symbol name) {
    LocalId id;
    return scope_local(scope, name, &id) && scope->depth.data[id.id] == scope->marks.len;
}

void scope_declare(Scope *scope, Symbol name, LocalId id) {
    Shadowed s = {.name = name, .previous = scope->local[name.id]};
    VEC_PUSH(scope->saved, s);
    scope->local[name.id] = id.id + 1;
    VEC_PUSH(scope->depth, (uint32_t)scope->marks.len);
}
