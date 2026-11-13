// `str` operations used by string interpolation.

#include "ofir_rt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void reserve(OfirStr *s, int64_t extra) {
    int64_t needed = s->len + extra;
    if (needed <= s->cap) {
        return;
    }
    int64_t cap = s->cap > 0 ? s->cap * 2 : 16;
    while (cap < needed) {
        cap *= 2;
    }
    if (s->cap == 0) {   // a literal or an empty string: copy to the heap
        uint8_t *heap = ofir_alloc(cap);
        if (s->len > 0) {
            memcpy(heap, s->ptr, (size_t)s->len);
        }
        s->ptr = heap;
    } else {
        s->ptr = ofir_realloc(s->ptr, cap);
    }
    s->cap = cap;
}

void ofir_str_push_bytes(OfirStr *s, const uint8_t *ptr, int64_t len) {
    if (len == 0) {
        return;
    }
    reserve(s, len);
    memcpy(s->ptr + s->len, ptr, (size_t)len);
    s->len += len;
}

void ofir_str_push_float(OfirStr *s, double value) {
    char text[OFIR_FORMAT_SIZE];
    ofir_str_push_bytes(s, (const uint8_t *)text, ofir_format_float(text, value));
}

void ofir_str_push_f32(OfirStr *s, float value) {
    char text[OFIR_FORMAT_SIZE];
    ofir_str_push_bytes(s, (const uint8_t *)text, ofir_format_f32(text, value));
}

void ofir_str_push_fixed(OfirStr *s, double value, int64_t digits) {
    if (isnan(value) || isinf(value)) {
        ofir_str_push_float(s, value);
        return;
    }
    int n = snprintf(NULL, 0, "%.*f", (int)digits, value);
    char small[OFIR_FORMAT_SIZE];
    char *text = n < OFIR_FORMAT_SIZE ? small : ofir_alloc(n + 1);
    snprintf(text, (size_t)n + 1, "%.*f", (int)digits, value);
    ofir_str_push_bytes(s, (const uint8_t *)text, n);
    if (text != small) {
        ofir_free(text);
    }
}

void ofir_str_drop(OfirStr *s) {
    if (s->cap > 0) {
        ofir_free(s->ptr);
    }
    *s = (OfirStr){0};
}

