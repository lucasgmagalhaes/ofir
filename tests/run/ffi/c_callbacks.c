#include <stdbool.h>
#include <stdint.h>

void c_each(int32_t n, void (*f)(void *, int32_t), void *data) {
    for (int32_t i = 1; i <= n; i++) {
        f(data, i * 10);
    }
}

static int32_t negate(int32_t v) {
    return -v;
}

int32_t (*c_negate(void))(int32_t) {
    return negate;
}

int32_t c_apply(int32_t (*f)(int32_t, bool), int32_t v) {
    return f(v, false) + f(v, true) * 2;
}

void c_fill(int64_t *out, int64_t len) {
    for (int64_t i = 0; i < len; i++) {
        out[i] = i * i;
    }
}
