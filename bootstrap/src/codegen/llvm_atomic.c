// LLVM IR for the atomic intrinsics (ADR 0042): `load atomic`,
// `store atomic`, `atomicrmw`, `cmpxchg` and `fence`. A `bool` is an `i1`
// in registers and a byte in memory; LLVM atomics need a whole byte, so a
// bool is widened to `i8` around the instruction.

#include "codegen/llvm_internal.h"

#include <string.h>

static const char *const ORDERS[] = {
    [ORDER_RELAXED] = "monotonic", [ORDER_ACQUIRE] = "acquire", [ORDER_RELEASE] = "release",
    [ORDER_ACQ_REL] = "acq_rel", [ORDER_SEQ_CST] = "seq_cst",
};

// The ordering of a failed compare-exchange: its load part.
static const char *const FAIL_ORDERS[] = {
    [ORDER_RELAXED] = "monotonic", [ORDER_ACQUIRE] = "acquire", [ORDER_RELEASE] = "monotonic",
    [ORDER_ACQ_REL] = "acquire", [ORDER_SEQ_CST] = "seq_cst",
};

static const char *const RMW_OPS[] = {
    [ATOMIC_SWAP] = "xchg", [ATOMIC_ADD] = "add", [ATOMIC_SUB] = "sub",
};

static unsigned align_of_llvm(const char *type) {
    if (strcmp(type, "i8") == 0 || strcmp(type, "i1") == 0) {
        return 1;
    }
    return strcmp(type, "i16") == 0 ? 2 : strcmp(type, "i32") == 0 ? 4 : 8;
}

// The value `v` as the atomic instruction takes it: an `i1` becomes `i8`.
static void widen(Gen *g, Value v, bool is_bool, char out[160]) {
    if (!is_bool) {
        strcpy(out, v.text);
        return;
    }
    next_temp(g, out);
    out_fmt(&g->out, "  %s = zext i1 %s to i8\n", out, v.text);
}

// The result of the instruction in `raw`, narrowed back to `i1` for a bool.
static void narrow(Gen *g, const char *raw, bool is_bool, Value *result) {
    if (!is_bool) {
        strcpy(result->text, raw);
        return;
    }
    next_temp(g, result->text);
    out_fmt(&g->out, "  %s = trunc i8 %s to i1\n", result->text, raw);
}

void gen_atomic(Gen *g, const Rvalue *rv, Value *result) {
    const char *order = ORDERS[rv->order];
    if (rv->atomic == ATOMIC_FENCE) {
        out_fmt(&g->out, "  fence %s\n", order);
        return;
    }
    TypeId inner = type_get(g->types, rv->a.type)->inner;
    const char *type = llvm_type(g, inner);
    bool is_bool = strcmp(type, "i1") == 0;
    type = is_bool ? "i8" : type;
    unsigned align = align_of_llvm(type);
    Value p = gen_operand(g, rv->a);
    char raw[16], value[160];
    switch (rv->atomic) {
    case ATOMIC_LOAD:
        next_temp(g, raw);
        out_fmt(&g->out, "  %s = load atomic %s, ptr %s %s, align %u\n", raw, type, p.text,
                order, align);
        narrow(g, raw, is_bool, result);
        return;
    case ATOMIC_STORE:
        widen(g, gen_operand(g, rv->b), is_bool, value);
        out_fmt(&g->out, "  store atomic %s %s, ptr %s %s, align %u\n", type, value, p.text,
                order, align);
        return;
    case ATOMIC_CAS: {
        char desired[160], pair[16];
        widen(g, gen_operand(g, rv->b), is_bool, value);
        widen(g, gen_operand(g, rv->items[0]), is_bool, desired);
        next_temp(g, pair);
        out_fmt(&g->out, "  %s = cmpxchg ptr %s, %s %s, %s %s %s %s, align %u\n", pair, p.text,
                type, value, type, desired, order, FAIL_ORDERS[rv->order], align);
        next_temp(g, raw);
        out_fmt(&g->out, "  %s = extractvalue { %s, i1 } %s, 0\n", raw, type, pair);
        narrow(g, raw, is_bool, result);
        return;
    }
    default:   // swap, add, sub
        widen(g, gen_operand(g, rv->b), is_bool, value);
        next_temp(g, raw);
        out_fmt(&g->out, "  %s = atomicrmw %s ptr %s, %s %s %s, align %u\n", raw,
                RMW_OPS[rv->atomic], p.text, type, value, order, align);
        narrow(g, raw, is_bool, result);
        return;
    }
}
