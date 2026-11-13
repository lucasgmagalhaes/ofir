#include "sema/builtins.h"
#include "sema/check_internal.h"
#include "sema/const_eval.h"

#include <float.h>
#include <math.h>
#include <string.h>

static const uint32_t INT_BITS[] = {8, 16, 32, 64, 8, 16, 32, 64};

typedef struct {
    Checker *c;
    bool failed;   // an error was reported: stop
} Eval;

static bool eval(Eval *ev, const Expr *e, ConstValue *out);

static bool fail(Eval *ev, const char *code, Span span, const char *message) {
    if (!ev->failed) {
        check_error(ev->c, code, span, "%s", message);
    }
    ev->failed = true;
    return false;
}

static const Ty *ty(Eval *ev, TypeId type) {
    return type_get(ev->c->types, type);
}

// A signed value as int64_t, from the bit pattern of its width.
static int64_t as_signed(uint64_t bits, uint32_t width) {
    if (width < 64 && (bits >> (width - 1)) & 1) {
        bits |= ~((1ull << width) - 1);
    }
    return (int64_t)bits;
}

// Whether the mathematical result fits the type: `wide` is exact as long as
// the 64-bit operation did not overflow.
static bool fits(const Ty *t, int64_t s, uint64_t u, bool is_signed_result) {
    uint32_t width = INT_BITS[t->data];
    if (t->data <= INT_I64) {
        int64_t min = width == 64 ? INT64_MIN : -(int64_t)(1ull << (width - 1));
        int64_t max = width == 64 ? INT64_MAX : (int64_t)(1ull << (width - 1)) - 1;
        return is_signed_result && s >= min && s <= max;
    }
    return !is_signed_result && (width == 64 || u <= (1ull << width) - 1);
}

static ConstValue int_value(TypeId type, uint64_t bits, uint32_t width) {
    uint64_t mask = width == 64 ? UINT64_MAX : (1ull << width) - 1;
    return (ConstValue){.kind = CV_INT, .type = type, .bits = bits & mask};
}

// `ab` holds both operands.
static bool int_arith(Eval *ev, Op op, const Expr *e, const ConstValue ab[2], ConstValue *out) {
    ConstValue a = ab[0], b = ab[1];
    const Ty *t = ty(ev, a.type);
    uint32_t width = INT_BITS[t->data];
    bool is_signed = t->data <= INT_I64;
    int64_t x = as_signed(a.bits, width), y = as_signed(b.bits, width), s = 0;
    uint64_t u = 0;
    bool overflow = false;
    if ((op == OP_DIV || op == OP_REM) && b.bits == 0) {
        return fail(ev, "E0802", e->span, "division by zero in a constant");
    }
    if (is_signed) {
        switch (op) {
        case OP_ADD: overflow = __builtin_add_overflow(x, y, &s); break;
        case OP_SUB: overflow = __builtin_sub_overflow(x, y, &s); break;
        case OP_MUL: overflow = __builtin_mul_overflow(x, y, &s); break;
        case OP_DIV: overflow = x == INT64_MIN && y == -1; s = overflow ? 0 : x / y; break;
        default: overflow = x == INT64_MIN && y == -1; s = overflow ? 0 : x % y; break;
        }
    } else {
        switch (op) {
        case OP_ADD: overflow = __builtin_add_overflow(a.bits, b.bits, &u); break;
        case OP_SUB: overflow = __builtin_sub_overflow(a.bits, b.bits, &u); break;
        case OP_MUL: overflow = __builtin_mul_overflow(a.bits, b.bits, &u); break;
        case OP_DIV: u = a.bits / b.bits; break;
        default: u = a.bits % b.bits; break;
        }
    }
    if (overflow || !fits(t, s, u, is_signed)) {
        return fail(ev, "E0802", e->span, "integer overflow in a constant");
    }
    *out = int_value(a.type, is_signed ? (uint64_t)s : u, width);
    return true;
}

static bool int_bits_op(Eval *ev, Op op, const Expr *e, const ConstValue ab[2],
                        ConstValue *out) {
    ConstValue a = ab[0], b = ab[1];
    const Ty *t = ty(ev, a.type);
    uint32_t width = INT_BITS[t->data];
    if ((op == OP_SHL || op == OP_SHR) && b.bits >= width) {   // also a negative amount
        return fail(ev, "E0802", e->span, "shift amount out of range in a constant");
    }
    uint64_t r;
    switch (op) {
    case OP_AND: r = a.bits & b.bits; break;
    case OP_OR: r = a.bits | b.bits; break;
    case OP_XOR: r = a.bits ^ b.bits; break;
    case OP_SHL: r = a.bits << b.bits; break;
    default:
        r = t->data <= INT_I64 ? (uint64_t)(as_signed(a.bits, width) >> b.bits) : a.bits >> b.bits;
        break;
    }
    *out = int_value(a.type, r, width);
    return true;
}

// <0, 0 or >0.
static int compare(Eval *ev, ConstValue a, ConstValue b) {
    switch (a.kind) {
    case CV_INT: {
        const Ty *t = ty(ev, a.type);
        if (t->data <= INT_I64) {
            uint32_t width = INT_BITS[t->data];
            int64_t x = as_signed(a.bits, width), y = as_signed(b.bits, width);
            return (x > y) - (x < y);
        }
        return (a.bits > b.bits) - (a.bits < b.bits);
    }
    case CV_FLOAT: return (a.f > b.f) - (a.f < b.f);
    case CV_BOOL: return (int)a.b - (int)b.b;
    case CV_STR: {
        size_t n = a.s.len < b.s.len ? a.s.len : b.s.len;
        int order = n ? memcmp(a.s.ptr, b.s.ptr, n) : 0;
        return order ? order : (a.s.len > b.s.len) - (a.s.len < b.s.len);
    }
    }
    return 0;
}

static bool comparison(Eval *ev, Op op, ConstValue a, ConstValue b, ConstValue *out) {
    bool unordered = a.kind == CV_FLOAT && (isnan(a.f) || isnan(b.f));
    int order = compare(ev, a, b);
    bool r;
    switch (op) {
    case OP_EQ: r = !unordered && order == 0; break;
    case OP_NE: r = unordered || order != 0; break;
    case OP_LT: r = !unordered && order < 0; break;
    case OP_LE: r = !unordered && order <= 0; break;
    case OP_GT: r = !unordered && order > 0; break;
    default: r = !unordered && order >= 0; break;
    }
    *out = (ConstValue){.kind = CV_BOOL, .type = type_id(TID_BOOL), .b = r};
    return true;
}

static bool float_arith(Op op, ConstValue a, ConstValue b, ConstValue *out) {
    double r;
    switch (op) {
    case OP_ADD: r = a.f + b.f; break;
    case OP_SUB: r = a.f - b.f; break;
    case OP_MUL: r = a.f * b.f; break;
    case OP_DIV: r = a.f / b.f; break;
    default: r = fmod(a.f, b.f); break;
    }
    *out = (ConstValue){.kind = CV_FLOAT, .type = a.type, .f = r};
    return true;
}

static bool concat(Eval *ev, ConstValue a, ConstValue b, ConstValue *out) {
    size_t len = a.s.len + b.s.len;
    char *text = ARENA_ARRAY(&ev->c->sema->session->arena, char, len + 1);
    memcpy(text, a.s.ptr, a.s.len);
    memcpy(text + a.s.len, b.s.ptr, b.s.len);
    *out = (ConstValue){.kind = CV_STR, .type = a.type, .s = {text, len}};
    return true;
}

static bool binary(Eval *ev, const Expr *e, ConstValue *out) {
    Op op = e->as.binary.op;
    ConstValue ab[2];
    ConstValue a, b;
    if (!eval(ev, e->as.binary.lhs, &a)) {
        return false;
    }
    if (op == OP_LOGICAL_AND || op == OP_LOGICAL_OR) {   // short-circuit, as at run time
        if (a.b == (op == OP_LOGICAL_OR)) {
            *out = a;
            return true;
        }
        return eval(ev, e->as.binary.rhs, out);
    }
    if (!eval(ev, e->as.binary.rhs, &b)) {
        return false;
    }
    if (op >= OP_EQ && op <= OP_GE) {
        return comparison(ev, op, a, b, out);
    }
    if (a.kind == CV_STR) {
        return concat(ev, a, b, out);
    }
    if (a.kind == CV_FLOAT) {
        return float_arith(op, a, b, out);
    }
    ab[0] = a;
    ab[1] = b;
    if (op >= OP_ADD && op <= OP_REM) {
        return int_arith(ev, op, e, ab, out);
    }
    return int_bits_op(ev, op, e, ab, out);
}

static bool unary(Eval *ev, const Expr *e, ConstValue *out) {
    ConstValue a;
    if (!eval(ev, e->as.unary.operand, &a)) {
        return false;
    }
    if (a.kind == CV_BOOL) {
        *out = (ConstValue){.kind = CV_BOOL, .type = a.type, .b = !a.b};
    } else if (a.kind == CV_FLOAT) {
        *out = (ConstValue){.kind = CV_FLOAT, .type = a.type, .f = -a.f};
    } else if (e->as.unary.op == OP_NOT) {
        *out = int_value(a.type, ~a.bits, INT_BITS[ty(ev, a.type)->data]);
    } else {
        ConstValue ab[2] = {int_value(a.type, 0, 64), a};
        return int_arith(ev, OP_SUB, e, ab, out);
    }
    return true;
}

static bool cast(Eval *ev, const Expr *e, ConstValue *out) {
    ConstValue a;
    if (!eval(ev, e->as.cast.value, &a)) {
        return false;
    }
    const Ty *to = ty(ev, e->type);
    if (to->kind == TY_FLOAT) {
        const Ty *from = ty(ev, a.type);
        double f = a.kind == CV_FLOAT ? a.f
                   : from->data <= INT_I64 ? (double)as_signed(a.bits, INT_BITS[from->data])
                                           : (double)a.bits;
        *out = (ConstValue){.kind = CV_FLOAT, .type = e->type, .f = to->data == 32 ? (float)f : f};
        return true;
    }
    if (a.kind == CV_FLOAT) {
        return fail(ev, "E0801", e->span,
                    "float to integer conversions are not allowed in constants");
    }
    if (to->kind == TY_PTR) {   // `0 as *T`, the null pointer: the only constant pointer
        if (a.bits != 0) {
            return fail(ev, "E0801", e->span, "the only constant pointer is `0 as *T`");
        }
        *out = (ConstValue){.kind = CV_INT, .type = e->type, .bits = 0};
        return true;
    }
    const Ty *from = ty(ev, a.type);
    bool from_signed = from->data <= INT_I64;
    int64_t value = from_signed ? as_signed(a.bits, INT_BITS[from->data]) : 0;
    bool negative = from_signed && value < 0;
    uint64_t magnitude = from_signed ? (uint64_t)value : a.bits;   // the value when not negative
    uint32_t width = INT_BITS[to->data];
    bool ok;
    if (to->data <= INT_I64) {
        int64_t min = width == 64 ? INT64_MIN : -(int64_t)(1ull << (width - 1));
        uint64_t max = (1ull << (width - 1)) - 1;
        ok = negative ? value >= min : magnitude <= max;
    } else {
        ok = !negative && (width == 64 || magnitude <= (1ull << width) - 1);
    }
    if (!ok) {
        return fail(ev, "E0802", e->span, "value out of range for the target type of `as`");
    }
    *out = int_value(e->type, magnitude, width);
    return true;
}

static bool literal(Eval *ev, const Expr *e, ConstValue *out) {
    const Ty *t = ty(ev, e->type);
    switch (e->kind) {
    case EXPR_INT:
        if (t->kind == TY_FLOAT) {
            *out = (ConstValue){.kind = CV_FLOAT, .type = e->type, .f = (double)e->as.int_value};
        } else {
            *out = int_value(e->type, e->as.int_value, INT_BITS[t->data]);
        }
        return true;
    case EXPR_FLOAT: *out = (ConstValue){.kind = CV_FLOAT, .type = e->type, .f = e->as.float_value};
        return true;
    case EXPR_BYTE: *out = int_value(e->type, e->as.byte_value, 8); return true;
    case EXPR_BOOL: *out = (ConstValue){.kind = CV_BOOL, .type = e->type, .b = e->as.bool_value};
        return true;
    default: *out = (ConstValue){.kind = CV_STR, .type = e->type, .s = e->as.str}; return true;
    }
}

static bool primitive_const(Eval *ev, const Expr *e, ConstValue *out) {
    const Ty *t = ty(ev, e->type);
    BuiltinConst which = builtin_const(&ev->c->sema->session->names, t, e->as.field.name);
    if (t->kind == TY_INT) {
        uint32_t width = INT_BITS[t->data];
        bool is_signed = t->data <= INT_I64;
        uint64_t max = width == 64 ? UINT64_MAX : (1ull << width) - 1;
        uint64_t v = which == BC_MAX ? (is_signed ? max >> 1 : max)
                                     : (is_signed ? 1ull << (width - 1) : 0);
        *out = int_value(e->type, v, width);
        return true;
    }
    double f = which == BC_NAN ? (double)NAN : which == BC_INFINITY ? (double)INFINITY
               : t->data == 32 ? (double)FLT_EPSILON : DBL_EPSILON;
    *out = (ConstValue){.kind = CV_FLOAT, .type = e->type, .f = f};
    return true;
}

static bool eval(Eval *ev, const Expr *e, ConstValue *out) {
    if (e->type.id == TID_ERROR) {
        ev->failed = true;   // already reported by the checker
        return false;
    }
    switch (e->kind) {
    case EXPR_INT: case EXPR_FLOAT: case EXPR_BYTE: case EXPR_BOOL: case EXPR_STR:
        return literal(ev, e, out);
    case EXPR_IDENT: {
        Def *def = e->as.ident.res.kind == RES_DEF ? sema_def(ev->c->sema,
                                                              (DefId){e->as.ident.res.index})
                                                   : NULL;
        if (!def || def->kind != DEF_CONST || !def->value) {
            return fail(ev, "E0801", e->span, "only constants can be used in a constant");
        }
        *out = *def->value;
        out->type = e->type;
        return true;
    }
    case EXPR_FIELD: return primitive_const(ev, e, out);
    case EXPR_UNARY: return unary(ev, e, out);
    case EXPR_BINARY: return binary(ev, e, out);
    case EXPR_CAST: return cast(ev, e, out);
    default:
        return fail(ev, "E0801", e->span,
                    "a constant may only use literals, constants and operators");
    }
}

ConstValue *const_eval(Checker *c, const Expr *e) {
    Eval ev = {.c = c};
    ConstValue value;
    if (!eval(&ev, e, &value)) {
        return NULL;
    }
    ConstValue *stored = ARENA_NEW(&c->sema->session->arena, ConstValue);
    *stored = value;
    return stored;
}
