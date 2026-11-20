// The few methods and constants of the primitive types that std cannot
// declare yet: `x.wrappingCast<u8>()`, `p.add(n)`, `i32.MAX`, ... The other
// methods are `@lang` declarations in std/core (ADR 0019).

#ifndef OFIR_BUILTINS_H
#define OFIR_BUILTINS_H

#include "base/intern.h"
#include "sema/types.h"
#include "syntax/ast.h"

// What stays built in until generic bounds: casts to an integer type
// argument and pointer offsets (ADR 0019). Everything else is in std.
typedef enum {
    BM_WRAPPING_CAST,
    BM_SATURATING_CAST,
    BM_CHECKED_CAST,
    BM_PTR_ADD,
    BM_CLONE,     // `x.clone()` of a type without a `clone` method (sema/check_clone.c)
    BM_COUNT,
} BuiltinMethod;

// How a parameter or the result relates to the receiver type.
typedef enum {
    BT_NONE,      // no value (no parameter / returns ())
    BT_SELF,      // the receiver type
    BT_INT,
    BT_TARGET,    // the type argument: x.wrappingCast<T>() returns T
    BT_OPTION,    // Option of the type argument: x.checkedCast<T>()
} BuiltinType;

typedef enum { RECV_INT, RECV_PTR } Receiver;

typedef struct {
    const char *name;
    Receiver receiver;
    BuiltinMethod method;
    Mode self_mode;        // MODE_MUT: the receiver must be a mutable place
    BuiltinType params[2];
    BuiltinType ret;
    bool type_arg;         // takes one integer type argument
} BuiltinInfo;

// The method `name` for a receiver, or NULL.
const BuiltinInfo *builtin_method(const Interner *names, Receiver receiver, Symbol name);

typedef enum { BC_NONE, BC_MIN, BC_MAX, BC_NAN, BC_INFINITY, BC_EPSILON } BuiltinConst;

// `I.MIN`, `I.MAX`, `F.NAN`, `F.INFINITY`, `F.EPSILON`.
BuiltinConst builtin_const(const Interner *names, const Ty *type, Symbol name);

#endif
