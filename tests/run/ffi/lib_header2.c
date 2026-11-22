// The C side of lib_header2.of: it includes the generated header.
#include <stdio.h>

#include "lib_header2.h"

static int32_t one(void) {
    return 1;
}

static int32_t mul(int32_t a, int32_t b) {
    return a * b;
}

int main(void) {
    struct Pair pair = {4, 5};
    const struct Pair *pp = &pair;
    int32_t *cell = NULL;
    printf("call %d\n", h2_call(one, mul));
    printf("first %d\n", h2_first(&pp, &cell));
    printf("opaque %d\n", h2_opaque(NULL, NULL, NULL));
    struct Grid grid = {{1, 2, 3, 4}};
    const struct Grid *gp = &grid;
    printf("named %d\n", h2_named(&gp, NULL, NULL, NULL));
    printf("cos %.1f\n", h2_cos(0.0));
    return 0;
}
