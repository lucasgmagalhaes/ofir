// State shared by the lowering files (typed AST → AIR).
//
// AIR local 0 is the return value and AST local `i` is AIR local `i + 1`, so
// `self` and the parameters (the first AST locals) are AIR locals
// 1..param_count. Temporaries follow.

#ifndef OFIR_LOWER_INTERNAL_H
#define OFIR_LOWER_INTERNAL_H

#include "air/lower.h"

typedef struct {
    BlockId break_to, continue_to;
    uint32_t scopes;        // drop scopes open outside the loop
    uint32_t temp_scopes;   // temporary scopes open outside the loop
} Loop;

// The drop flag of a part of a local that is moved out somewhere.
typedef struct {
    MovedPart part;   // `local` is the AIR local
    LocalId flag;
} PartFlag;

// A temporary to drop at the end of its full expression.
typedef struct {
    LocalId local;
    TypeId type;
} Temp;

typedef struct {
    LocalId local;
    FieldPath path;
} Place;

// A function to compile: a function that is not generic (targs `()`), or
// an instance of a generic one.
typedef struct {
    FnDecl *fn;      // NULL for glue
    TypeId targs;
    DefId glue;      // GLUE_DROP or GLUE_CLONE when `fn` is NULL
} Instance;

typedef struct {
    Sema *sema;
    TypeTable *types;
    AirBody *body;
    BlockId block;          // where the next statement goes
    Vec(Loop) loops;
    Vec(Instance) pending;  // functions reached by a call, still to lower
    Vec(bool) reached;      // by DefId, for functions that are not generic
    Vec(LocalId) owned;     // locals to drop at the end of their scope, in order
    Vec(uint32_t) scopes;   // per open block: its first entry in `owned`
    LocalId *flags;         // per local of the function: its drop flag (0: none)
    uint32_t flag_count;
    const Local *fn_locals; // the function's locals: which ones borrow
    const FnDecl *fn;       // the function being lowered (NULL for drop glue)
    Vec(PartFlag) part_flags;   // of this function's partially moved locals
    Vec(uint8_t) drop_cache;    // by TypeId: 0 unknown, 1 no drop, 2 drop, 3 computing
    Vec(Temp) temps;        // new values no one consumed, to drop
    Vec(uint32_t) temp_scopes;   // per open full expression: its first temporary
    bool forward;           // the next expression's value is the value of an `if`,
                            // `match` or block: it is that expression's temporary
    uint64_t *instances;    // the generic instances reached: open addressing of
    uint32_t instance_count, instance_cap;   // DefId << 32 | targs, 0 = empty
    bool too_deep;          // E0331 was reported
} Lowerer;

static inline LocalId air_local_of(LocalId ast_local) {
    return (LocalId){ast_local.id + 1};
}

static inline Operand op_copy(LocalId local, TypeId type) {
    return (Operand){.kind = OPERAND_COPY, .type = type, .as.local = local};
}

static inline Operand op_int(uint64_t value, TypeId type) {
    return (Operand){.kind = OPERAND_INT, .type = type, .as.int_value = value};
}

static inline Operand op_bool(bool value) {
    return (Operand){.kind = OPERAND_BOOL, .type = type_id(TID_BOOL), .as.bool_value = value};
}

static inline Operand op_unit(void) {
    return (Operand){.kind = OPERAND_UNIT, .type = type_id(TID_UNIT)};
}

// lower.c: blocks and statements
LocalId new_temp(Lowerer *l, TypeId type);
void emit_assign(Lowerer *l, LocalId dest, Rvalue rvalue);
LocalId emit_temp(Lowerer *l, TypeId type, Rvalue rvalue);
void emit_runtime(Lowerer *l, RuntimeFn fn, const Operand *args, uint32_t count, LocalId *dest);
void terminate(Lowerer *l, Terminator term);   // ends the block, continues in a new one
void emit_goto(Lowerer *l, BlockId target);
void switch_to(Lowerer *l, BlockId block);
void emit_assert(Lowerer *l, Operand fails, PanicKind panic, Span span);
Operand lower_block(Lowerer *l, Block *block, bool as_value);
Operand lower_tail(Lowerer *l, Expr *e);   // the value of a block or an arm
Operand lower_condition(Lowerer *l, Expr *e);   // its temporaries end with it
// lower_stmt.c: the body of a loop; `latch` is where `continue` goes.
void lower_loop_body(Lowerer *l, Block *body, BlockId latch, BlockId exit);
LocalId lower_global_addr(Lowerer *l, uint32_t def);   // a `*mut T` temporary
LocalId to_local(Lowerer *l, Operand operand);   // a local holding the operand

// lower_expr.c
Operand lower_expr(Lowerer *l, Expr *e);
Operand emit_user_call(Lowerer *l, Expr *e, FnDecl *fn, Expr *receiver);
bool type_is_value(TypeId type);   // false for () and !

typedef struct {
    Operand *items;
    uint32_t count;
} CallArgs;

// Calls `fn` with the type arguments `targs` (a tuple, or ()); mono_body
// reaches it. `dest` receives the result unless it is NULL.
void emit_direct_call(Lowerer *l, const FnDecl *fn, TypeId targs, CallArgs args,
                      LocalId *dest);
// The function `name` of the std module `module` (`std.core.hash`).
FnDecl *std_fn(Lowerer *l, const char *module, const char *name);
// Whether parameter `index` of `fn` (-1 for `self`) is passed by address: a
// `mut` parameter, or the borrowed value a returned reference points into.
bool by_address(const FnDecl *fn, int32_t index);

// mono.c: monomorphization (docs/design/compiler-architecture.md, 10)
// Queues `fn` with the type arguments `targs` (a tuple, `()` if it is not
// generic) to be lowered: only functions reachable from `main` are.
void reach(Lowerer *l, FnDecl *fn, TypeId targs);
// Substitutes the instance's type arguments into a lowered body, then
// reaches the functions it calls.
void mono_body(Lowerer *l, AirBody *body);
void mono_free(Lowerer *l);

// lower_ops.c
typedef struct {
    Op op;
    TypeId type;     // the type of both operands
    Operand a, b;
    Span span;       // where a failed check is reported
} Arith;

Operand lower_binary_op(Lowerer *l, Arith x);
Operand lower_unary(Lowerer *l, Expr *e);
Operand lower_cast(Lowerer *l, Operand value, TypeId to, Span span);
Operand lower_interpolation(Lowerer *l, Expr *e);
bool lower_push_interpolation(Lowerer *l, Expr *call, const FnDecl *fn);
void lower_print(Lowerer *l, Expr *arg, bool newline);

// lower_method.c
Operand lower_method_call(Lowerer *l, Expr *call);
Operand lower_field(Lowerer *l, Expr *e);
Operand lower_index(Lowerer *l, Expr *e);
void lower_panic_assert(Lowerer *l, Expr *call, Builtin builtin);
void emit_bounds_check(Lowerer *l, Operand index, Operand len, Span span);

// lower_list.c: `List<T>` literals, indexing and loops
bool is_list(Lowerer *l, TypeId type);
Operand lower_list_literal(Lowerer *l, Expr *e);
Operand lower_list_repeat(Lowerer *l, Expr *e);
Place lower_list_index(Lowerer *l, Expr *e);   // the element's place, bounds checked
void lower_for_list(Lowerer *l, Stmt *s);

// lower_pattern.c
Place place_child(Lowerer *l, Place place, uint32_t step);
Operand build_variant(Lowerer *l, TypeId type, uint32_t index, ArgList args);
Operand variant_value(Lowerer *l, TypeId type, uint32_t index, const Operand *fields,
                      uint32_t count);
Operand is_variant(Lowerer *l, Place place, TypeId type, uint32_t index);   // a bool
Place variant_field(Lowerer *l, Place place, uint32_t index, uint32_t field);

// lower_option.c: `?` and `??`
Operand lower_try(Lowerer *l, Expr *e);
Operand lower_coalesce(Lowerer *l, Expr *e);
Operand enum_equality(Lowerer *l, Op op, Operand a, Operand b);
Operand lower_tuple(Lowerer *l, Expr *e);
// Tests `p` against the value at `place`: jumps to `fail` if it does not
// match, else continues in the current block with the bindings assigned.
void lower_pattern(Lowerer *l, Pattern *p, Place place, BlockId fail);
void keep_unbound(Lowerer *l, const Pattern *p, Place place);
void keep_place(Lowerer *l, Place place, TypeId type);
Operand lower_match(Lowerer *l, Expr *e);

// lower_drop.c: drop glue (spec 5.2)
TypeId concrete_type(Lowerer *l, TypeId type);   // with the instance's type arguments
bool needs_drop(Lowerer *l, TypeId type);         // `type` is concrete
void emit_drop(Lowerer *l, Place place, TypeId type);
void lower_glue(Lowerer *l, TypeId targs, BodySink sink, void *context);
Operand lower_drop_intrinsic(Lowerer *l, Expr *call, bool forget);
FnDecl *type_method(Lowerer *l, TypeId type, StrView name);
TypeId method_targs(Lowerer *l, TypeId type);

// lower_iter.c: `for x in it` over a type with `fn next(mut self) Option<T>`
bool is_iterator(Lowerer *l, TypeId type);
void lower_for_iter(Lowerer *l, Stmt *s);

// lower_test.c: `ofir test`
void find_tests(Lowerer *l, uint32_t module);
void lower_test_main(Lowerer *l, BodySink sink, void *context);

// lower_clone.c
Operand clone_value(Lowerer *l, Operand value, TypeId type);   // `type` is concrete
void lower_clone_glue(Lowerer *l, TypeId targs, BodySink sink, void *context);
Operand lower_clone_call(Lowerer *l, Expr *call);               // the derived `x.clone()`
Operand lower_clone_of(Lowerer *l, Expr *call);                 // `cloneOf<T>(value)`

// lower_scope.c: drop scopes and drop flags
void drop_scope_enter(Lowerer *l);
void drop_scope_leave(Lowerer *l);                     // drops the scope's locals
void drop_scopes(Lowerer *l, uint32_t first);     // for `return`, `break`, `continue`
bool drop_scope_owns(Lowerer *l);                      // the innermost scope owns a local
void own_local(Lowerer *l, LocalId local, bool initialized);
void own_bindings(Lowerer *l, const Pattern *p);
void drop_moved(Lowerer *l, LocalId local);
void drop_before_assign(Lowerer *l, LocalId local);
void assign_done(Lowerer *l, LocalId local);
void note_moved(Lowerer *l, const Expr *e);
bool place_path(const Expr *e, LocalId *local, uint32_t *path, uint32_t *len);
bool assign_part(Lowerer *l, const Expr *target, Place place, TypeId type);
void temp_scope_enter(Lowerer *l);
void temp_scope_leave(Lowerer *l);                // drops its temporaries
void drop_temps(Lowerer *l, uint32_t first);      // for the jumps that leave them
// A new value no one consumes: dropped at the end of the full expression.
Operand own_temp(Lowerer *l, const Expr *e, Operand value);

// lower_intrinsic.c
Operand lower_intrinsic(Lowerer *l, Expr *call, FnDecl *fn);

// lower_hash.c: `hashOf<K>` and `==` on tuples, for the instance's type
Operand lower_hash_of(Lowerer *l, Expr *call);
// `a == b` for a concrete type: element by element for tuples.
Operand lower_equal_values(Lowerer *l, Operand a, Operand b, TypeId type);

// lower_struct.c
Place lower_place(Lowerer *l, Expr *e);
Operand place_operand(Place place, OperandKind kind, TypeId type);
Operand lower_field_read(Lowerer *l, Expr *e);
Operand lower_struct_literal(Lowerer *l, Expr *e);
Operand lower_constructor(Lowerer *l, Expr *call);
void emit_store(Lowerer *l, Place place, Operand value);
// Reads a variable now when `next`, evaluated after it, could assign to it
// (a block or an `if`): evaluation is left to right (spec 4.10).
Operand settle(Lowerer *l, Operand value, const Expr *next);

#endif
