// The C side of lib_static.of: it includes the generated header.
#include <stdio.h>
#include <string.h>

#include "lib_static.h"

static int64_t square(int32_t i) {
    return (int64_t)i * i;
}

int main(void) {
    struct Point p = {3, -4};
    printf("add %d\n", calc_add(2, 3));
    printf("norm %lld\n", (long long)calc_norm1(&p));
    printf("sum %lld\n", (long long)calc_sum(5, square));
    fflush(stdout);
    const char *name = "C";
    calc_greet((const uint8_t *)name, (int64_t)strlen(name));
    ofir_stdout_flush();
    calc_greet((const uint8_t *)"again", 5);
    return 0;
}
