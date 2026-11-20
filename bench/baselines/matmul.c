// Dense matrix multiply: the C baseline of programs/matmul.of.
#include <stdio.h>
#include <stdlib.h>

static double *fill(long n, long seed) {
    double *m = malloc(sizeof(double) * n * n);
    for (long i = 0; i < n * n; i++) {
        m[i] = (double)((i * seed) % 17) - 8.0;
    }
    return m;
}

static void multiply(const double *a, const double *b, double *c, long n) {
    for (long i = 0; i < n; i++) {
        for (long k = 0; k < n; k++) {
            double aik = a[i * n + k];
            for (long j = 0; j < n; j++) {
                c[i * n + j] += aik * b[k * n + j];
            }
        }
    }
}

int main(void) {
    long n = 500;
    double *a = fill(n, 7);
    double *b = fill(n, 5);
    double *c = calloc((size_t)(n * n), sizeof(double));
    multiply(a, b, c, n);
    double trace = 0.0, total = 0.0;
    for (long i = 0; i < n; i++) {
        trace += c[i * n + i];
    }
    for (long i = 0; i < n * n; i++) {
        total += c[i];
    }
    printf("%.1f %.1f\n", trace, total);
    free(a);
    free(b);
    free(c);
    return 0;
}
