// A numerical kernel with the shape of BLAS's `cblas_dgemm` (ADR 0100):
// row-major matrices given as a pointer, a size and a leading dimension.
// C = alpha * A * B + beta * C, where A is m x k, B is k x n and C is m x n.

#include <stdint.h>

void nk_dgemm(int64_t m, int64_t n, int64_t k, double alpha, const double *a, int64_t lda,
              const double *b, int64_t ldb, double beta, double *c, int64_t ldc) {
    for (int64_t i = 0; i < m; i++) {
        for (int64_t j = 0; j < n; j++) {
            double sum = 0.0;
            for (int64_t p = 0; p < k; p++) {
                sum += a[i * lda + p] * b[p * ldb + j];
            }
            c[i * ldc + j] = alpha * sum + beta * c[i * ldc + j];
        }
    }
}
