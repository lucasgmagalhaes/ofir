#include "base/str.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

StrView sv_from_cstr(const char *s) {
    return (StrView){s, strlen(s)};
}

bool sv_eq(StrView a, StrView b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

// FNV-1a: simple, deterministic (no seed), good enough for identifiers.
uint64_t sv_hash(StrView s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < s.len; i++) {
        h ^= (unsigned char)s.ptr[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

static void reserve(StrBuf *buf, size_t extra) {
    size_t needed = buf->len + extra + 1;   // + terminating zero
    if (needed <= buf->cap) {
        return;
    }
    size_t cap = buf->cap ? buf->cap * 2 : 64;
    while (cap < needed) {
        cap *= 2;
    }
    buf->data = xrealloc(buf->data, cap);
    buf->cap = cap;
}

void strbuf_free(StrBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = buf->cap = 0;
}

void strbuf_clear(StrBuf *buf) {
    buf->len = 0;
    if (buf->data) {
        buf->data[0] = '\0';
    }
}

void strbuf_push(StrBuf *buf, char c) {
    reserve(buf, 1);
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
}

void strbuf_append(StrBuf *buf, StrView s) {
    reserve(buf, s.len);
    if (s.len) {
        memcpy(buf->data + buf->len, s.ptr, s.len);
    }
    buf->len += s.len;
    buf->data[buf->len] = '\0';
}

void strbuf_append_cstr(StrBuf *buf, const char *s) {
    strbuf_append(buf, sv_from_cstr(s));
}

void strbuf_appendf(StrBuf *buf, const char *format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (n < 0) {
        fatal("invalid format string: %s", format);
    }
    reserve(buf, (size_t)n);
    vsnprintf(buf->data + buf->len, (size_t)n + 1, format, args);
    buf->len += (size_t)n;
    va_end(args);
}

StrView strbuf_view(const StrBuf *buf) {
    return (StrView){buf->data ? buf->data : "", buf->len};
}
