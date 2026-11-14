// The C side of lib_shared.of.
#include <stdio.h>

#include "lib_shared.h"

int main(void) {
    int64_t state = 0;
    counter_next(&state);
    counter_next(&state);
    printf("next %lld\n", (long long)counter_next(&state));
    fflush(stdout);
    counter_report(&state);
    return 0;
}
