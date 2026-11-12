// Range checks of `as` conversions (spec 2.1): whether a value fits the
// target integer type. The result is an i1 that is true when it does not.

#include "codegen/llvm_internal.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

static const uint32_t INT_BITS[] = {8, 16, 32, 64, 8, 16, 32, 64};

static bool is_signed(const Ty *ty) {
    return ty->data <= INT_I64;
}

// Combines two optional failure conditions with `or`.
static void either(Gen *g, const char *low, const char *high, Value *result) {
    if (low[0] && high[0]) {
        next_temp(g, result->text);
        out_fmt(&g->out, "  %s = or i1 %s, %s\n", result->text, low, high);
    } else {
        fmt_into(result->text, sizeof result->text, "%s", low[0] ? low : high[0] ? high : "false");
    }
}

// The target bounds that a value of the source type can exceed.
typedef struct {
    bool below, above;   // whether the minimum / maximum must be checked
    int64_t min;
    uint64_t max;
} Bounds;

static Bounds bounds(const Ty *from, const Ty *to) {
    uint32_t s = INT_BITS[from->data], t = INT_BITS[to->data];
    Bounds b = {.below = is_signed(from) && (!is_signed(to) || t < s)};
    b.above = is_signed(from) ? (is_signed(to) ? t < s : t + 1 < s)
                              : (is_signed(to) ? t <= s : t < s);
    b.min = !is_signed(to) ? 0 : t == 64 ? INT64_MIN : -(int64_t)(1ull << (t - 1));
    b.max = is_signed(to) ? (1ull << (t - 1)) - 1 : t == 64 ? UINT64_MAX : (1ull << t) - 1;
    return b;
}

// Integer to integer: compare with the target bounds the source can exceed.
static void int_fails(Gen *g, Value a, const Ty *from, const Ty *to, Value *result) {
    Bounds b = bounds(from, to);
    char low[16] = "", high[16] = "";
    if (b.below) {
        next_temp(g, low);
        out_fmt(&g->out, "  %s = icmp slt %s %s, %" PRId64 "\n", low, a.type, a.text, b.min);
    }
    if (b.above) {
        next_temp(g, high);
        out_fmt(&g->out, "  %s = icmp %s %s %s, %" PRIu64 "\n", high,
                is_signed(from) ? "sgt" : "ugt", a.type, a.text, b.max);
    }
    either(g, low, high, result);
}

// `x.saturatingCast<T>()`: clamp in the source type, then convert.
void gen_saturate(Gen *g, const Rvalue *rv, Value *result) {
    Value a = gen_operand(g, rv->a);
    const Ty *from = type_get(g->types, rv->a.type), *to = type_get(g->types, rv->to);
    Bounds b = bounds(from, to);
    char value[160], test[16];
    fmt_into(value, sizeof value, "%s", a.text);
    if (b.below) {
        next_temp(g, test);
        out_fmt(&g->out, "  %s = icmp slt %s %s, %" PRId64 "\n", test, a.type, value, b.min);
        char clamped[16];
        next_temp(g, clamped);
        out_fmt(&g->out, "  %s = select i1 %s, %s %" PRId64 ", %s %s\n", clamped, test, a.type,
                b.min, a.type, value);
        fmt_into(value, sizeof value, "%s", clamped);
    }
    if (b.above) {
        next_temp(g, test);
        out_fmt(&g->out, "  %s = icmp %s %s %s, %" PRIu64 "\n", test,
                is_signed(from) ? "sgt" : "ugt", a.type, value, b.max);
        char clamped[16];
        next_temp(g, clamped);
        out_fmt(&g->out, "  %s = select i1 %s, %s %" PRIu64 ", %s %s\n", clamped, test, a.type,
                b.max, a.type, value);
        fmt_into(value, sizeof value, "%s", clamped);
    }
    Value clamped = {.type = a.type};
    fmt_into(clamped.text, sizeof clamped.text, "%s", value);
    gen_cast_value(g, rv->a.type, rv->to, clamped, result);
}

static void float_bound(char *out, double bound) {
    uint64_t bits;
    memcpy(&bits, &bound, sizeof bits);
    fmt_into(out, 32, "0x%016" PRIX64, bits);
}

// Float to integer: the value truncated toward zero must fit, so it must be
// in (min - 1, max + 1). NaN fails both ordered comparisons. When min - 1 is
// not exact in the source type, no value lies between it and min, and the
// test is `>= min`. The bounds are powers of two, exact in f32 and f64.
static void float_fails(Gen *g, Value a, const Ty *from, const Ty *to, Value *result) {
    uint32_t t = INT_BITS[to->data];
    double min = is_signed(to) ? -ldexp(1.0, (int)t - 1) : 0.0;
    double end = is_signed(to) ? ldexp(1.0, (int)t - 1) : ldexp(1.0, (int)t);
    double below = min - 1.0;
    bool exact = from->data == 32 ? (double)(float)below == below : below != min;
    const char *op = exact ? "ogt" : "oge";
    char lo[32], hi[32], ge[16], lt[16], ok[16];
    float_bound(lo, exact ? below : min);
    float_bound(hi, end);
    next_temp(g, ge);
    out_fmt(&g->out, "  %s = fcmp %s %s %s, %s\n", ge, op, a.type, a.text, lo);
    next_temp(g, lt);
    out_fmt(&g->out, "  %s = fcmp olt %s %s, %s\n", lt, a.type, a.text, hi);
    next_temp(g, ok);
    out_fmt(&g->out, "  %s = and i1 %s, %s\n", ok, ge, lt);
    next_temp(g, result->text);
    out_fmt(&g->out, "  %s = xor i1 %s, true\n", result->text, ok);
}

void gen_cast_fails(Gen *g, const Rvalue *rv, Value *result) {
    Value a = gen_operand(g, rv->a);
    const Ty *from = type_get(g->types, rv->a.type), *to = type_get(g->types, rv->to);
    result->type = "i1";
    if (from->kind == TY_FLOAT) {
        float_fails(g, a, from, to, result);
    } else {
        int_fails(g, a, from, to, result);
    }
}
