// Building and splitting a string: the C baseline of programs/strings.of.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const int64_t n = 1000000;
    size_t cap = 16, len = 0;
    char *text = malloc(cap);
    char part[32];
    for (int64_t i = 0; i < n; i++) {
        int k = snprintf(part, sizeof part, "%lld,", (long long)(i * 7));
        while (len + (size_t)k > cap) {
            cap *= 2;
            text = realloc(text, cap);
        }
        memcpy(text + len, part, (size_t)k);
        len += (size_t)k;
    }
    int64_t parts = 0, digits = 0;
    size_t start = 0;
    for (;;) {
        char *comma = memchr(text + start, ',', len - start);
        size_t end = comma ? (size_t)(comma - text) : len;
        parts++;
        digits += (int64_t)(end - start);
        if (!comma) {
            break;
        }
        start = end + 1;
    }
    printf("%zu %lld %lld\n", len, (long long)parts, (long long)digits);
    free(text);
    return 0;
}
