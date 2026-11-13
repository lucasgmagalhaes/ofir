// Tests of the structural AIR validator (air/air_validate.c): each malformed
// body is rejected with a message naming the function, the block and the fault.

#include "air/air_print.h"
#include "test.h"

#include <string.h>

// fn1(x) { _0 = x; return }, the smallest valid body: 2 locals, 1 parameter.
static AirBody valid_body(void) {
    AirBody body = {.def = {1}, .param_count = 1};
    air_new_local(&body, type_id(TID_INT), (Symbol){0}, (Span){0});
    air_new_local(&body, type_id(TID_INT), (Symbol){0}, (Span){0});
    BlockId entry = air_new_block(&body);
    AirStmt assign = {.kind = AIR_ASSIGN, .dest = {0}, .has_dest = true};
    assign.as.rvalue.kind = RV_USE;
    assign.as.rvalue.a = (Operand){.kind = OPERAND_COPY, .type = type_id(TID_INT)};
    assign.as.rvalue.a.as.local.id = 1;
    air_push(&body, entry, assign);
    air_terminate(&body, entry, (Terminator){.kind = TERM_RETURN});
    return body;
}

// Whether `body` is rejected with `expected` as the message.
static bool rejects(const AirBody *body, const char *expected) {
    char error[256] = "";
    bool valid = air_validate_body(body, error, sizeof error);
    if (valid || strcmp(error, expected) != 0) {
        fprintf(stderr, "  got `%s`, expected `%s`\n", valid ? "valid" : error, expected);
        return false;
    }
    return true;
}

static void test_valid(void) {
    AirBody body = valid_body();
    char error[256] = "";
    CHECK(air_validate_body(&body, error, sizeof error));
    air_body_free(&body);
}

static void test_missing_parts(void) {
    AirBody body = {.def = {1}, .param_count = 1};
    CHECK(rejects(&body, "AIR of `fn1`, bb0: missing return local, parameters or entry block"));
    air_body_free(&body);
}

static void test_unterminated(void) {
    AirBody body = valid_body();
    air_new_block(&body);
    CHECK(rejects(&body, "AIR of `fn1`, bb1: block is not terminated"));
    air_body_free(&body);
}

static void test_missing_local(void) {
    AirBody body = valid_body();
    air_block(&body, (BlockId){0})->stmts.data[0].as.rvalue.a.as.local.id = 9;
    CHECK(rejects(&body, "AIR of `fn1`, bb0: statement refers to a missing local"));
    air_body_free(&body);
}

static void test_missing_block(void) {
    AirBody body = valid_body();
    air_block(&body, (BlockId){0})->term = (Terminator){.kind = TERM_GOTO, .target = {7}};
    CHECK(rejects(&body, "AIR of `fn1`, bb0: jump to a missing block"));
    Operand cond = {.kind = OPERAND_BOOL, .type = type_id(TID_BOOL)};
    air_block(&body, (BlockId){0})->term =
        (Terminator){.kind = TERM_BRANCH, .cond = cond, .target = {0}, .other = {3}};
    CHECK(rejects(&body, "AIR of `fn1`, bb0: branch to a missing block"));
    air_body_free(&body);
}

static void test_missing_condition(void) {
    AirBody body = valid_body();
    Operand cond = {.kind = OPERAND_COPY, .type = type_id(TID_BOOL), .as.local = {5}};
    air_block(&body, (BlockId){0})->term =
        (Terminator){.kind = TERM_ASSERT, .cond = cond, .target = {0}};
    CHECK(rejects(&body, "AIR of `fn1`, bb0: condition refers to a missing local"));
    air_block(&body, (BlockId){0})->term = (Terminator){.kind = TERM_PANIC, .cond = cond};
    CHECK(rejects(&body, "AIR of `fn1`, bb0: message refers to a missing local"));
    air_body_free(&body);
}

int main(void) {
    test_valid();
    test_missing_parts();
    test_unterminated();
    test_missing_local();
    test_missing_block();
    test_missing_condition();
    return test_report("air_validate");
}
