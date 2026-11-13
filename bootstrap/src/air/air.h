// AIR, the Ofir Intermediate Representation (docs/design/compiler-architecture.md,
// 7): a control-flow graph of basic blocks per function. Every operand is a
// local or a constant; control flow and runtime checks are explicit.
//
// Stage 0 differences from the design:
// - the only projection is a field: a place is a local and a path of field
//   positions (`p.pos.x` is local p with path [pos, x]);
// - calls are statements, not terminators: Ofir has no unwinding, so a
//   call either returns or ends the process;
// - a call to a function returning `!` is followed by `Unreachable`.

#ifndef OFIR_AIR_H
#define OFIR_AIR_H

#include "base/str.h"
#include "base/vec.h"
#include "sema/ids.h"
#include "syntax/ast.h"

typedef struct {
    uint32_t id;
} BlockId;

typedef enum {
    OPERAND_COPY,     // the value of a local
    OPERAND_ADDR,     // the address of a local (runtime calls taking a pointer)
    OPERAND_INT,      // integer constant of `type`
    OPERAND_FLOAT,
    OPERAND_BOOL,
    OPERAND_STR,      // string literal
    OPERAND_UNIT,
} OperandKind;

// A step of a field path that views an enum's payload as variant `v`: the
// next step is a field of that variant. Step 0 of an enum is its tag.
#define PATH_VARIANT 0x80000000u

// The field path of a place: positions of struct fields from the local.
// With `deref`, the local holds the address of the value (a `mut`
// parameter): the path starts from that address.
typedef struct {
    const uint32_t *fields;
    uint32_t len;
    bool deref;
} FieldPath;

typedef struct {
    OperandKind kind;
    TypeId type;
    union {
        LocalId local;       // OPERAND_COPY and OPERAND_ADDR, with `path`
        uint64_t int_value;
        double float_value;
        bool bool_value;
        StrView str;
    } as;
    FieldPath path;          // empty for a whole local
} Operand;

typedef enum {
    RV_USE,           // a
    RV_BINARY,        // a op b, unchecked (comparisons, floats, bitwise, wrapping)
    RV_CHECKED,       // a op b for + - *; `flag` receives the overflow bit
    RV_UNARY,         // -a (floats), !a
    RV_CAST,          // a as T
    RV_CAST_FAILS,    // whether `a as T` is out of range (bool)
    RV_SATURATE,      // a converted to T, clamped to its range
    RV_STR_LEN,       // the length of the str a
    RV_STR_BYTE,      // the byte b of the str a (bounds already checked)
    RV_FLOAT_FN,      // fn(a) or fn(a, b): sqrt, fabs, floor, ceil, minnum, maxnum
    RV_AGGREGATE,     // a struct value from its fields, in declaration order
    RV_PTR_ADD,       // the pointer a moved by b elements
    RV_SIZE_OF,       // the size in bytes of a value of type `to` (an int)
    RV_ALIGN_OF,      // the alignment in bytes of type `to` (an int)
} RvalueKind;

typedef enum { FF_SQRT, FF_FABS, FF_FLOOR, FF_CEIL, FF_MINNUM, FF_MAXNUM, FF_COUNT } FloatFn;

typedef struct {
    RvalueKind kind;
    Op op;
    Operand a, b;
    TypeId to;        // casts
    LocalId flag;     // RV_CHECKED
    FloatFn fn;       // RV_FLOAT_FN
    Operand *items;   // RV_AGGREGATE
    uint32_t count;
} Rvalue;

// Functions of the runtime library (runtime/include/ofir_rt.h).
typedef enum {
    RT_PRINT_STR, RT_PRINT_INT, RT_PRINT_UINT, RT_PRINT_FLOAT, RT_PRINT_F32, RT_PRINT_BOOL,
    RT_PRINT_NEWLINE,
    RT_STR_PUSH_STR, RT_STR_PUSH_INT, RT_STR_PUSH_UINT, RT_STR_PUSH_FLOAT, RT_STR_PUSH_F32,
    RT_STR_PUSH_BOOL,
    RT_STR_CONCAT,
    RT_TEST_INDEX,    // the test `ofir test` asked for, or -1
    RT_COUNT,
} RuntimeFn;

typedef enum {
    AIR_ASSIGN,       // dest = rvalue
    AIR_CALL,         // dest = callee(args); dest may be absent
} AirStmtKind;

typedef struct {
    AirStmtKind kind;
    LocalId dest;
    FieldPath dest_path;     // assignments to a field
    bool has_dest;
    union {
        Rvalue rvalue;
        struct {
            bool runtime;       // a runtime function, else a user function
            RuntimeFn rt;
            DefId fn;           // 0 for the drop glue of the type in `targs`
            TypeId targs;       // the type arguments of a generic fn, a tuple; else ()
            Operand *args;
            uint32_t arg_count;
        } call;
    } as;
} AirStmt;

typedef enum {
    PANIC_OVERFLOW,
    PANIC_DIV_ZERO,
    PANIC_SHIFT,
    PANIC_CAST,
    PANIC_BOUNDS,     // with the index and the length
    PANIC_ASSERT,
} PanicKind;

typedef enum {
    TERM_NONE,        // not terminated yet (only while lowering)
    TERM_GOTO,
    TERM_BRANCH,      // if cond goto then else goto other
    TERM_ASSERT,      // if cond panics with `panic`, else goto next
    TERM_RETURN,      // returns local 0
    TERM_PANIC,       // panic(cond): `cond` is the address of the message (a str)
    TERM_UNREACHABLE,
} TermKind;

typedef struct {
    TermKind kind;
    Operand cond;
    BlockId target, other;   // goto/next: target; branch: target (true), other (false)
    PanicKind panic;
    Operand index, len;      // PANIC_BOUNDS
    Span span;               // where an assertion fails
} Terminator;

typedef struct {
    Vec(AirStmt) stmts;
    Terminator term;
} AirBlock;

typedef struct {
    TypeId type;
    Symbol name;      // 0 for temporaries
    Span span;
    bool by_ref;      // a `mut` parameter: holds the address of a `type` value
} AirLocal;

// Local 0 is the return value; locals 1..param_count are the parameters.
// The body of an instance of a generic function has its types substituted
// (air/mono.c): no TY_PARAM remains.
typedef struct {
    DefId def;        // 0 for drop glue: `targs` is then the tuple of the dropped type
    TypeId targs;     // the type arguments of the instance, a tuple; () if not generic
    Symbol name;
    bool is_main;
    uint32_t param_count;
    Vec(AirLocal) locals;
    Vec(AirBlock) blocks;   // block 0 is the entry
} AirBody;

void air_body_free(AirBody *body);

// Building blocks, used by the lowering.
BlockId air_new_block(AirBody *body);
LocalId air_new_local(AirBody *body, TypeId type, Symbol name, Span span);
void air_push(AirBody *body, BlockId block, AirStmt stmt);
void air_terminate(AirBody *body, BlockId block, Terminator term);

static inline AirBlock *air_block(AirBody *body, BlockId id) {
    return &body->blocks.data[id.id];
}

const char *runtime_fn_name(RuntimeFn fn);

// The functions the compiler generates per type: in an AIR body or call,
// `def` is one of these and `targs` is the tuple of the type.
#define GLUE_DROP 0u            // drop<T>(p: &mut T)
#define GLUE_CLONE 0xFFFFFFFFu  // clone<T>(p: &T) T

static inline bool is_glue(DefId def) {
    return def.id == GLUE_DROP || def.id == GLUE_CLONE;
}

#endif
