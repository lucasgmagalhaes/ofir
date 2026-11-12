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

void ofir_str_push_str(OfirStr *s, const OfirStr *part) {
    ofir_str_push_bytes(s, part->ptr, part->len);
}

void ofir_str_push_int(OfirStr *s, int64_t value) {
    char text[OFIR_FORMAT_SIZE];
    ofir_str_push_bytes(s, (const uint8_t *)text, ofir_format_int(text, value));
}

void ofir_str_push_uint(OfirStr *s, uint64_t value) {
    char text[OFIR_FORMAT_SIZE];
    ofir_str_push_bytes(s, (const uint8_t *)text, ofir_format_uint(text, value));
}

void ofir_str_push_float(OfirStr *s, double value) {
    char text[OFIR_FORMAT_SIZE];
    ofir_str_push_bytes(s, (const uint8_t *)text, ofir_format_float(text, value));
}

void ofir_str_push_f32(OfirStr *s, float value) {
    char text[OFIR_FORMAT_SIZE];
    ofir_str_push_bytes(s, (const uint8_t *)text, ofir_format_f32(text, value));
}

void ofir_str_push_bool(OfirStr *s, bool value) {
    ofir_str_push_bytes(s, (const uint8_t *)(value ? "true" : "false"), value ? 4 : 5);
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

void ofir_str_push_radix(OfirStr *s, uint64_t value, int64_t format) {
    uint64_t base = (uint64_t)(format & 0xff);
    int64_t bits = (format >> 8) & 0xff;
    if (bits < 64) {
        value &= ((uint64_t)1 << bits) - 1;
    }
    const char *digits = (format >> 16) & 1 ? "0123456789ABCDEF" : "0123456789abcdef";
    char text[64];
    int n = 0;
    do {
        text[63 - n++] = digits[value % base];
        value /= base;
    } while (value != 0);
    ofir_str_push_bytes(s, (const uint8_t *)text + 64 - n, n);
}

void ofir_str_push_padded(OfirStr *s, const OfirStr *part, int64_t padding) {
    int64_t width = padding & 0xffffffff;
    bool left = (padding >> 32) & 1;
    bool zero = (padding >> 33) & 1;
    int64_t count = 0;   // the width counts Unicode scalar values
    for (int64_t i = 0; i < part->len; i++) {
        count += (part->ptr[i] & 0xc0) != 0x80;
    }
    int64_t pad = width > count ? width - count : 0;
    int64_t sign = zero && part->len > 0 && (part->ptr[0] == '-' || part->ptr[0] == '+');
    if (left) {
        ofir_str_push_bytes(s, part->ptr, part->len);
    } else {
        ofir_str_push_bytes(s, part->ptr, sign);
    }
    if (pad > 0) {
        reserve(s, pad);
        memset(s->ptr + s->len, zero ? '0' : ' ', (size_t)pad);
        s->len += pad;
    }
    if (!left) {
        ofir_str_push_bytes(s, part->ptr + sign, part->len - sign);
    }
}

void ofir_str_push_debug(OfirStr *s, const OfirStr *part) {
    static const char hex[] = "0123456789abcdef";
    ofir_str_push_bytes(s, (const uint8_t *)"\"", 1);
    for (int64_t i = 0; i < part->len; i++) {
        uint8_t c = part->ptr[i];
        char esc = c == '"' ? '"' : c == '\\' ? '\\' : c == '\n' ? 'n' : c == '\t' ? 't'
                 : c == '\r' ? 'r' : c == 0 ? '0' : 0;
        if (esc) {
            uint8_t pair[2] = {'\\', (uint8_t)esc};
            ofir_str_push_bytes(s, pair, 2);
        } else if (c < 0x20 || c == 0x7f) {
            uint8_t code[4] = {'\\', 'x', (uint8_t)hex[c >> 4], (uint8_t)hex[c & 15]};
            ofir_str_push_bytes(s, code, 4);
        } else {
            ofir_str_push_bytes(s, &c, 1);
        }
    }
    ofir_str_push_bytes(s, (const uint8_t *)"\"", 1);
}

void ofir_str_drop(OfirStr *s) {
    if (s->cap > 0) {
        ofir_free(s->ptr);
    }
    *s = (OfirStr){0};
}

void ofir_str_concat(OfirStr *out, const OfirStr *a, const OfirStr *b) {
    OfirStr result = {0};
    ofir_str_push_bytes(&result, a->ptr, a->len);
    ofir_str_push_bytes(&result, b->ptr, b->len);
    *out = result;
}

bool ofir_str_eq(const OfirStr *a, const OfirStr *b) {
    return a->len == b->len && (a->len == 0 || memcmp(a->ptr, b->ptr, (size_t)a->len) == 0);
}

int32_t ofir_str_cmp(const OfirStr *a, const OfirStr *b) {
    int64_t common = a->len < b->len ? a->len : b->len;
    int order = common > 0 ? memcmp(a->ptr, b->ptr, (size_t)common) : 0;
    if (order != 0) {
        return order < 0 ? -1 : 1;
    }
    return a->len < b->len ? -1 : a->len > b->len;
}

int64_t ofir_find_bytes(const uint8_t *text, int64_t len, const uint8_t *part, int64_t n) {
    if (n == 0) {
        return 0;
    }
    if (n > len) {
        return -1;
    }
    const uint8_t *at = text;
    const uint8_t *last = text + (len - n);   // the last place a match can start
    while (at <= last) {
        at = memchr(at, part[0], (size_t)(last - at + 1));
        if (!at) {
            return -1;
        }
        if (n == 1 || memcmp(at + 1, part + 1, (size_t)(n - 1)) == 0) {
            return at - text;
        }
        at++;
    }
    return -1;
}
