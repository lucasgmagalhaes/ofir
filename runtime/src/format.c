// Number formatting for printing and interpolation.

#include "ofir_rt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// "00" to "99": two digits per division.
static const char DIGIT_PAIRS[201] =
    "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
    "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

int ofir_format_uint(char *buf, uint64_t value) {
    int n = 1;   // the number of digits
    for (uint64_t rest = value; rest >= 10; rest /= 10) {
        n++;
    }
    int at = n;
    while (value >= 100) {
        unsigned pair = (unsigned)(value % 100) * 2;
        value /= 100;
        buf[--at] = DIGIT_PAIRS[pair + 1];
        buf[--at] = DIGIT_PAIRS[pair];
    }
    if (value >= 10) {
        buf[--at] = DIGIT_PAIRS[value * 2 + 1];
        buf[--at] = DIGIT_PAIRS[value * 2];
    } else {
        buf[--at] = (char)('0' + value);
    }
    return n;
}

int ofir_format_int(char *buf, int64_t value) {
    if (value >= 0) {
        return ofir_format_uint(buf, (uint64_t)value);
    }
    buf[0] = '-';
    return 1 + ofir_format_uint(buf + 1, 0 - (uint64_t)value);   // also for INT64_MIN
}

// Removes the `+` and the leading zeros of an exponent: `1e+07` -> `1e7`.
static int tidy_exponent(char *buf, int n) {
    char *e = strchr(buf, 'e');
    if (!e) {
        return n;
    }
    char *digits = e + 1;
    char *out = digits;
    if (*digits == '-') {
        out++;
        digits++;
    } else if (*digits == '+') {
        digits++;
    }
    while (*digits == '0' && digits[1] != '\0') {
        digits++;
    }
    memmove(out, digits, strlen(digits) + 1);
    return (int)strlen(buf);
}

// The shortest decimal form that reads back as the same value (spec 2.1):
// fixed notation for magnitudes in [1e-5, 1e16), an exponent otherwise, and
// always a fraction or an exponent, so that `1.0` is not printed as `1`.
static int format_float(char *buf, double value, bool is_f32) {
    if (isnan(value)) {
        memcpy(buf, "NaN", 4);
        return 3;
    }
    if (isinf(value)) {
        strcpy(buf, value > 0 ? "inf" : "-inf");
        return value > 0 ? 3 : 4;
    }
    double magnitude = fabs(value);
    bool fixed = magnitude == 0 || (magnitude >= 1e-5 && magnitude < 1e16);
    int n = 0;
    for (int precision = fixed ? 0 : 1; precision <= (fixed ? 22 : 17); precision++) {
        int digits = fixed ? precision : precision - 1;
        n = snprintf(buf, OFIR_FORMAT_SIZE, fixed ? "%.*f" : "%.*e", digits, value);
        bool exact = is_f32 ? strtof(buf, NULL) == (float)value : strtod(buf, NULL) == value;
        if (exact) {
            break;
        }
    }
    n = tidy_exponent(buf, n);
    if (!strpbrk(buf, ".e")) {
        memcpy(buf + n, ".0", 3);
        n += 2;
    }
    return n;
}

int ofir_format_float(char *buf, double value) {
    return format_float(buf, value, false);
}

int ofir_format_f32(char *buf, float value) {
    return format_float(buf, value, true);
}
