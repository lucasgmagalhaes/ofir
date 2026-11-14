#include "base/common.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xmalloc(size_t size) {
    void *ptr = malloc(size ? size : 1);
    if (!ptr) {
        fatal("out of memory");
    }
    return ptr;
}

void *xcalloc(size_t count, size_t size) {
    void *ptr = calloc(count ? count : 1, size ? size : 1);
    if (!ptr) {
        fatal("out of memory");
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *result = realloc(ptr, size ? size : 1);
    if (!result) {
        fatal("out of memory");
    }
    return result;
}

void fatal(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("ofir: internal error: ", stderr);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    abort();
}

void print_float_g(FILE *out, double v) {
    if (isnan(v)) {
        fputs(signbit(v) ? "-nan" : "nan", out);
        return;
    }
    if (isinf(v)) {
        fputs(v < 0 ? "-inf" : "inf", out);
        return;
    }
    char text[64];
    snprintf(text, sizeof text, "%g", v);
    char *e = strchr(text, 'e');
    if (e != NULL && (e[1] == '+' || e[1] == '-')) {
        char *digits = e + 2;
        size_t n = strlen(digits);
        size_t skip = 0;
        while (n - skip > 2 && digits[skip] == '0') {
            skip++;
        }
        memmove(digits, digits + skip, n - skip + 1);
    }
    fputs(text, out);
}
