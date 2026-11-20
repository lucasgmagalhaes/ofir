#include "codegen/out.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define FILE_BUFFER (256 * 1024)

void out_to_file(Out *out, FILE *file) {
    *out = (Out){.file = file, .buf = xmalloc(FILE_BUFFER), .cap = FILE_BUFFER};
}

void out_to_memory(Out *out) {
    *out = (Out){0};
}

void out_to_array(Out *out, char *array, size_t size) {
    *out = (Out){.buf = array, .cap = size, .fixed = true};
    array[0] = '\0';
}

void out_flush(Out *out) {
    if (out->file && out->len > 0) {
        fwrite(out->buf, 1, out->len, out->file);
        out->len = 0;
    }
}

void out_free(Out *out) {
    out_flush(out);
    if (!out->fixed) {
        free(out->buf);
    }
    *out = (Out){0};
}

static void grow(Out *out, size_t needed) {
    size_t cap = out->cap ? out->cap : 4096;
    while (cap < needed) {
        cap *= 2;
    }
    out->buf = xrealloc(out->buf, cap);
    out->cap = cap;
}

// Invariant: len < cap, so the NUL after the text always fits.
void out_write_slow(Out *out, const char *text, size_t len) {
    if (out->fixed) {   // truncate
        size_t room = out->cap - out->len - 1;
        len = len < room ? len : room;
    } else if (out->file) {
        out_flush(out);
        if (len >= out->cap) {
            fwrite(text, 1, len, out->file);
            return;
        }
    } else {
        grow(out, out->len + len + 1);
    }
    memcpy(out->buf + out->len, text, len);
    out->len += len;
    out->buf[out->len] = '\0';
}

void out_u64(Out *out, uint64_t value) {
    char digits[24];
    size_t n = sizeof digits;
    do {
        digits[--n] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    out_write(out, digits + n, sizeof digits - n);
}

void out_i64(Out *out, int64_t value) {
    if (value < 0) {
        out_char(out, '-');
        out_u64(out, 0 - (uint64_t)value);
    } else {
        out_u64(out, (uint64_t)value);
    }
}

static void out_hex(Out *out, uint64_t value, int width) {
    static const char DIGITS[] = "0123456789ABCDEF";
    char text[16];
    for (int i = width - 1; i >= 0; i--) {
        text[i] = DIGITS[value & 15];
        value >>= 4;
    }
    out_write(out, text, (size_t)width);
}

// Handles one conversion starting after '%'; returns the rest of the format.
static const char *conversion(Out *out, const char *f, va_list *args) {
    if (f[0] == '0') {   // %02X, %016lX, %016llX
        int width = f[1] == '2' ? 2 : 16;
        f += width == 2 ? 2 : 3;
        while (*f == 'l') {
            f++;
        }
        out_hex(out, width == 2 ? va_arg(*args, unsigned) : va_arg(*args, unsigned long long),
                width);
        return f + 1;
    }
    int longs = 0;
    while (*f == 'l' || *f == 'z') {
        longs = *f == 'z' ? 3 : longs + 1;
        f++;
    }
    switch (*f) {
    case 's': out_str(out, va_arg(*args, const char *)); break;
    case 'c': out_char(out, (char)va_arg(*args, int)); break;
    case '%': out_char(out, '%'); break;
    case 'd':
        out_i64(out, longs == 0 ? va_arg(*args, int) : longs == 1 ? va_arg(*args, long)
                                                                  : va_arg(*args, long long));
        break;
    case 'u':
        out_u64(out, longs == 0 ? va_arg(*args, unsigned)
                     : longs == 1 ? va_arg(*args, unsigned long)
                     : longs == 2 ? va_arg(*args, unsigned long long) : va_arg(*args, size_t));
        break;
    // GCOVR_EXCL_START: a compiler bug: the formats are constants
    default: fatal("out_fmt: unsupported conversion `%%%c`", *f);
    // GCOVR_EXCL_STOP
    }
    return f + 1;
}

static void out_vfmt(Out *out, const char *format, va_list *args) {
    const char *f = format;
    for (;;) {
        const char *percent = strchr(f, '%');
        if (!percent) {
            out_str(out, f);
            return;
        }
        out_write(out, f, (size_t)(percent - f));
        f = conversion(out, percent + 1, args);
    }
}

void out_fmt(Out *out, const char *format, ...) {
    va_list args;
    va_start(args, format);
    out_vfmt(out, format, &args);
    va_end(args);
}

void fmt_into(char *array, size_t size, const char *format, ...) {
    Out out;
    out_to_array(&out, array, size);
    va_list args;
    va_start(args, format);
    out_vfmt(&out, format, &args);
    va_end(args);
}
