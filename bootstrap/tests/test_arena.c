#include "base/arena.h"
#include "base/vec.h"
#include "test.h"

#include <string.h>

static void test_alignment_and_zeroing(void) {
    Arena arena;
    arena_init(&arena);
    char *c = arena_alloc(&arena, 1, 1);
    double *d = ARENA_NEW(&arena, double);
    CHECK(((uintptr_t)d % _Alignof(double)) == 0);
    CHECK(*d == 0.0);
    *c = 'x';
    CHECK(*c == 'x');
    arena_free(&arena);
}

static void test_large_allocations(void) {
    Arena arena;
    arena_init(&arena);
    for (int i = 0; i < 100; i++) {
        unsigned char *block = arena_alloc(&arena, 10000, 8);
        block[9999] = 1;
    }
    unsigned char *huge = arena_alloc(&arena, 1 << 20, 16);
    huge[(1 << 20) - 1] = 2;
    CHECK(arena.total >= 100 * 10000 + (1 << 20));
    arena_free(&arena);
}

static void test_reset_keeps_oldest_chunk(void) {
    Arena arena;
    arena_init(&arena);
    void *first = arena_alloc(&arena, 10000, 8);
    for (int i = 0; i < 20; i++) {   // several chunks
        arena_alloc(&arena, 10000, 8);
    }
    arena_reset(&arena);
    CHECK(arena.total == 0);
    CHECK(arena_alloc(&arena, 10000, 8) == first);   // the oldest chunk is reused
    arena_free(&arena);
}

static void test_strndup(void) {
    Arena arena;
    arena_init(&arena);
    char *s = arena_strndup(&arena, "hello world", 5);
    CHECK(strcmp(s, "hello") == 0);
    arena_free(&arena);
}

static void test_vec(void) {
    Vec(int) v = {0};
    for (int i = 0; i < 1000; i++) {
        VEC_PUSH(v, i);
    }
    CHECK(v.len == 1000);
    CHECK(v.data[999] == 999);
    CHECK(VEC_POP(v) == 999);
    CHECK(VEC_LAST(v) == 998);
    VEC_FREE(v);
    CHECK(v.len == 0);
}

int main(void) {
    test_alignment_and_zeroing();
    test_large_allocations();
    test_reset_keeps_oldest_chunk();
    test_strndup();
    test_vec();
    return test_report("arena");
}
