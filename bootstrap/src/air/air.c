#include "air/air.h"

static const char *const RUNTIME_NAMES[RT_COUNT] = {
    [RT_PRINT_STR] = "ofir_print_str",
    [RT_PRINT_INT] = "ofir_print_int",
    [RT_PRINT_UINT] = "ofir_print_uint",
    [RT_PRINT_FLOAT] = "ofir_print_float",
    [RT_PRINT_F32] = "ofir_print_f32",
    [RT_PRINT_BOOL] = "ofir_print_bool",
    [RT_PRINT_NEWLINE] = "ofir_print_newline",
    [RT_STR_PUSH_STR] = "ofir_str_push_str",
    [RT_STR_PUSH_INT] = "ofir_str_push_int",
    [RT_STR_PUSH_UINT] = "ofir_str_push_uint",
    [RT_STR_PUSH_FLOAT] = "ofir_str_push_float",
    [RT_STR_PUSH_F32] = "ofir_str_push_f32",
    [RT_STR_PUSH_BOOL] = "ofir_str_push_bool",
    [RT_STR_CONCAT] = "ofir_str_concat",
    [RT_TEST_INDEX] = "ofir_test_index",
};

const char *runtime_fn_name(RuntimeFn fn) {
    return RUNTIME_NAMES[fn];
}

BlockId air_new_block(AirBody *body) {
    VEC_PUSH(body->blocks, (AirBlock){0});
    return (BlockId){(uint32_t)body->blocks.len - 1};
}

LocalId air_new_local(AirBody *body, TypeId type, Symbol name, Span span) {
    VEC_PUSH(body->locals, ((AirLocal){.type = type, .name = name, .span = span}));
    return (LocalId){(uint32_t)body->locals.len - 1};
}

void air_push(AirBody *body, BlockId block, AirStmt stmt) {
    VEC_PUSH(air_block(body, block)->stmts, stmt);
}

void air_terminate(AirBody *body, BlockId block, Terminator term) {
    air_block(body, block)->term = term;
}

void air_body_free(AirBody *body) {
    for (size_t b = 0; b < body->blocks.len; b++) {
        VEC_FREE(body->blocks.data[b].stmts);
    }
    VEC_FREE(body->blocks);
    VEC_FREE(body->locals);
}
