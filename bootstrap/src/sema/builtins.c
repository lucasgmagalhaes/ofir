#include "sema/builtins.h"

#include <string.h>

static const BuiltinInfo TABLE[] = {
    {"wrappingCast", RECV_INT, BM_WRAPPING_CAST, MODE_BORROW, {BT_NONE, BT_NONE}, BT_TARGET, true},
    {"saturatingCast", RECV_INT, BM_SATURATING_CAST, MODE_BORROW, {BT_NONE, BT_NONE}, BT_TARGET,
     true},
    {"checkedCast", RECV_INT, BM_CHECKED_CAST, MODE_BORROW, {BT_NONE, BT_NONE}, BT_OPTION, true},
    {"add", RECV_PTR, BM_PTR_ADD, MODE_BORROW, {BT_INT, BT_NONE}, BT_SELF, false},
};

const BuiltinInfo *builtin_method(const Interner *names, Receiver receiver, Symbol name) {
    StrView text = symbol_name(names, name);
    for (size_t i = 0; i < ARRAY_LEN(TABLE); i++) {
        if (TABLE[i].receiver == receiver && sv_eq(text, sv_from_cstr(TABLE[i].name))) {
            return &TABLE[i];
        }
    }
    return NULL;
}

BuiltinConst builtin_const(const Interner *names, const Ty *type, Symbol name) {
    StrView text = symbol_name(names, name);
    if (type->kind == TY_INT) {
        return sv_eq(text, SV("MIN")) ? BC_MIN : sv_eq(text, SV("MAX")) ? BC_MAX : BC_NONE;
    }
    if (type->kind == TY_FLOAT) {
        return sv_eq(text, SV("NAN"))        ? BC_NAN
               : sv_eq(text, SV("INFINITY")) ? BC_INFINITY
               : sv_eq(text, SV("EPSILON"))  ? BC_EPSILON
                                             : BC_NONE;
    }
    return BC_NONE;
}
