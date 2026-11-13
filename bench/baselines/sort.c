// Sorting a million integers: the C baseline of programs/sort.of.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int compare(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

int main(void) {
    const int64_t n = 1000000;
    int64_t *xs = malloc(n * sizeof *xs);
    int64_t seed = 42;
    for (int64_t i = 0; i < n; i++) {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        xs[i] = seed;
    }
    qsort(xs, n, sizeof *xs, compare);
    int64_t sum = 0;
    for (int64_t i = 0; i < n; i += 1000) {
        sum += xs[i] % 1000 * (i % 7);
    }
    printf("%lld %lld %lld %lld\n", (long long)xs[0], (long long)xs[n / 2],
           (long long)xs[n - 1], (long long)sum);
    free(xs);
    return 0;
}
