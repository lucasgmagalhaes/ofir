// State shared by the type checker files.
//
// Expressions are checked bidirectionally: check_expr receives the type the
// context expects (TID_ERROR when there is none) as a hint, synthesizes the
// type and stores it in the node; `coerce` then reports a mismatch.
// Divergence is a type: `return`, `break` and infinite loops have type `!`.

#ifndef OFIR_CHECK_INTERNAL_H
#define OFIR_CHECK_INTERNAL_H

#include "sema/sema.h"

// The type parameters in scope: those of the type declaring the method,
// numbered first, then those of the function (spec 8.3).
typedef struct {
    SymbolList owner, fn;
    WhereList owner_bounds, fn_bounds;   // their bounds (spec 8.1)
    const TypeId *alias_args; // actual arguments when expanding an alias
} Generics;

typedef struct {
    Sema *sema;
    TypeTable *types;
    Generics generics;
    FnDecl *fn;              // the function being checked
    TypeId ret;              // its return type
    TypeId self_type;        // the type declaring the method, or TID_ERROR
    TypeId *local_types;     // indexed by LocalId
    Vec(bool) loops;         // per open loop: whether a `break` leaves it
    uint32_t unsafe_depth;   // > 0 inside an `unsafe` block or function
    Arena scratch;           // reset per function: the move analysis states
    // Whether the function moves a value that is not trivially Copy (into a
    // variable, a `move` parameter, an aggregate, its result) or has a `let`
    // without a value: else the move analysis has nothing to find, and the
    // drops treat every new value as a temporary.
    bool owned_values;
} Checker;

// moves.c: whether a function needs the move analysis (Checker.owned_values)
void note_owned(Checker *c, TypeId type);         // a value of `type` may be moved
void note_aggregate(Checker *c, const Expr *e);   // an aggregate moves its parts in

// note_aggregate for a value that is not trivially Copy, tested inline:
// it runs for every expression.
static inline void note_value(Checker *c, const Expr *e) {
    TyKind kind = type_get(c->types, e->type)->kind;
    if (kind > TY_FLOAT && kind != TY_REF && kind != TY_PTR) {
        note_aggregate(c, e);
    }
}
void note_signature(Checker *c, const FnDecl *fn, const Ty *sig, TypeId self_type);

#define NO_TYPE type_id(TID_ERROR)   // "no expected type" for check_expr

void check_error(Checker *c, const char *code, Span span, const char *format, ...)
    __attribute__((format(printf, 4, 5)));

// Branches of `if` or `match` of different types: E0353 when no type is
// expected (a union is never inferred, spec 17.3), else E0300.
static inline void mismatched_branches(Checker *c, Span span, TypeId expected, const char *first,
                                       const char *other) {
    if (expected.id == TID_ERROR) {
        check_error(c, "E0353", span,
                    "the branches have different types, `%s` and `%s`: write the union type "
                    "expected", first, other);
    } else {
        check_error(c, "E0300", span, "mismatched types: expected %s, found %s", first, other);
    }
}

// Reports E0901 for a construct the stage 0 checker does not implement yet.
TypeId unsupported(Checker *c, Span span, const char *what);

// The spelling of a type for messages; valid until the next call.
const char *type_name(Checker *c, TypeId type);

// check_types.c
TypeId resolve_type(Checker *c, const TypeExpr *type);
// Makes `generics` the type parameters in scope; returns the previous ones.
Generics enter_generics(Checker *c, Generics generics);
// `Name<args>` for a struct or enum, checking the number of arguments
// (E0302); `args` NULL means none were written.
TypeId adt_instance(Checker *c, DefId id, const TypeId *args, uint32_t count, Span span);
TypeId fn_signature(Checker *c, FnDecl *fn);   // the TY_FN type, cached in the Def
// A declaration of another module named in a type or pattern (E0200, E0202).
DefId module_type(Checker *c, uint32_t module, Symbol name, Span span);
// Reports E0202 when a field or method of another module is not `pub`.
void check_visible(Checker *c, uint32_t module, bool is_pub, Symbol name, Span span);
// `!` converts to every type; the error type is compatible with everything
// so that one mistake is reported once.
static inline bool types_compatible(TypeId found, TypeId expected) {
    return type_eq(found, expected) || found.id == TID_ERROR || expected.id == TID_ERROR ||
           found.id == TID_NEVER;
}

// check_expr.c
TypeId check_expr(Checker *c, Expr *e, TypeId expected);
TypeId coerce(Checker *c, Expr *e, TypeId expected);
// Reports E0300 unless `found`, the type of the checked `e`, fits `expected`.
void require_type(Checker *c, const Expr *e, TypeId found, TypeId expected);
TypeId check_const(Checker *c, Def *def);   // the type of a constant or global
TypeId check_args(Checker *c, Expr *call, const Ty *sig);
TypeId check_unsafe_block(Checker *c, Expr *e, TypeId expected, bool as_value);

// const_eval.c: the value of a constant's initializer, or NULL after an error
struct ConstValue *const_eval(Checker *c, const Expr *e);

// check_method.c: methods, fields and indexing
TypeId check_method_call(Checker *c, Expr *call, TypeId expected);
TypeId check_field(Checker *c, Expr *e, TypeId expected);
TypeId check_index(Checker *c, Expr *e);
// Reports E0315 unless `e` is a place that may be modified.
void require_mutable(Checker *c, Expr *e);
// The variable at the root of a place: `p` in `p.pos.x`, or NULL.
Expr *place_root(Expr *e);

// check_unsafe.c
void require_unsafe(Checker *c, Span span, const char *what);   // E0803
TypeId check_deref(Checker *c, Expr *e, TypeId expected);
bool pointer_cast(Checker *c, TypeId from, TypeId to, Span span, bool zero);
void check_c_signature(Checker *c, FnDecl *fn);                  // E0804
bool is_unsafe_fn(const Def *def);
// A function of an `extern "intrinsic"` block: an operation the compiler
// generates (ADR 0019); safe to call.
bool is_intrinsic(const Def *def);

// check_generic.c: type arguments of generic calls, literals and variants
// What fixes the type arguments of a generic use besides its values: the
// type named before `.f()` or the receiver's type (`self`), and the type
// the context expects. Either may be TID_ERROR: unknown.
typedef struct {
    TypeId self, expected;
} TypeHints;

typedef struct {
    Generics params;     // the parameters, for messages
    TypeId *args;        // args[i]: the argument of parameter i, once known
    bool *known;
    uint32_t count;
} Infer;

Generics fn_generics(const Def *fn);

// The bounds stage 0 knows (spec 8.1); others are a stage 0 limit (E0901).
enum { BOUND_EQ = 1, BOUND_ORD = 2, BOUND_HASH = 4 };
// The bounds of a type parameter in scope, or 0 for other types.
uint32_t param_bounds(Checker *c, TypeId type);
// Whether a type satisfies a bound.
bool satisfies(Checker *c, TypeId type, uint32_t bit);
// E0332 for each argument that does not satisfy its parameter's bounds;
// E0901 for a struct, enum or tuple that stage 0 cannot compare or hash.
void check_bounds(Checker *c, Generics params, const TypeId *args, uint32_t count, Span span);
// E0901 for the bounds stage 0 does not know.
void check_bound_names(Checker *c, WhereList bounds);
Infer infer_new(Checker *c, Generics params);
// The explicit arguments `list` of the type's parameters (`of_owner`) or of
// the function's own (E0302).
void infer_explicit(Checker *c, Infer *inf, bool of_owner, TypeList list, Span span);
// Binds the parameters of `type` from `self`, an instance of the same type.
void infer_from_self(Infer *inf, const Ty *self, uint32_t count);
// Binds the parameters in `pattern` by matching it against `actual`.
void infer_from(Checker *c, Infer *inf, TypeId pattern, TypeId actual);
// `pattern` with the arguments substituted, or NO_TYPE if some are unknown.
TypeId infer_hint(Checker *c, const Infer *inf, TypeId pattern);
TypeId infer_apply(Checker *c, const Infer *inf, TypeId pattern);
// Reports E0330 for each parameter that is still unknown.
void infer_finish(Checker *c, Infer *inf, Span span);
// check_args for a generic callee: binds its parameters from the expected
// type and the arguments, then checks the arguments against the result.
TypeId check_generic_args(Checker *c, Expr *call, const Ty *sig, Infer *inf, TypeId expected);

// check_ref.c: references (spec 5.6)
TypeId check_ref(Checker *c, Expr *e, TypeId expected);
bool holds_ref(Checker *c, TypeId type);
// E0702 when `type` holds a reference; `where` names the place in the message.
void check_storable(Checker *c, TypeId type, Span span, const char *where);
void check_instance_storable(Checker *c, TypeId type, Span span);
// Sets `returns_borrow` and `borrow_source` of a function returning a reference.
void check_borrow_source(Checker *c, FnDecl *fn, const Ty *sig, TypeId self_type);
void check_returned_ref(Checker *c, const Expr *value);   // E0702 for `return &local`

// check_box.c: `Box<T>`, dereferenced by the compiler
// Whether `type` is `Box<T>`; T goes to `inner`.
bool box_inner(Checker *c, TypeId type, TypeId *inner);
// `*b` for a box `b` of type `box`: rewritten to `*(b.ptr)`; returns T.
TypeId box_deref(Checker *c, Expr *deref, TypeId box);
// Replaces `*place`, of type `type`, by its value while it is a box (except
// in the module declaring Box) or a reference; returns the type of the
// result.
TypeId auto_deref(Checker *c, Expr **place, TypeId type);
TypeId deref_once(Checker *c, Expr **place, TypeId type);   // one box or reference
// The receiver of `place.name()`: through references, and through boxes
// unless `Box` itself has the method.
TypeId method_receiver(Checker *c, Expr **place, TypeId type, Symbol name);
// Whether the dereference `e` reads through a box (`*(b.ptr)`).
bool is_box_deref(Checker *c, const Expr *e);

// check_list.c: `List<T>` literals, indexing and loops
bool list_elem(Checker *c, TypeId type, TypeId *elem);   // whether `type` is List<elem>
bool sequence_elem(Checker *c, TypeId type, TypeId *elem);   // List<elem> or &[elem]
TypeId check_range_index(Checker *c, Expr *e);   // `xs[a..b]`: becomes `xs.slice(a, b)`
// Rewrites `e`, a `List<T>`, to `e.asSlice()` when a `&[T]` is expected.
bool coerce_to_slice(Checker *c, Expr *e, TypeId found, TypeId expected);
void require_copy(Checker *c, Span span, TypeId type, const char *what);   // E0321
TypeId check_list_literal(Checker *c, Expr *e, TypeId expected);
TypeId check_list_repeat(Checker *c, Expr *e, TypeId expected);
// `for x in list`, `for i, x in list`, `for key, value in map`.
void check_for_collection(Checker *c, Stmt *s);
bool map_parts(Checker *c, TypeId type, TypeId *key, TypeId *value);   // Map<key, value>
void check_list_layout(Checker *c, const Def *def);

// check_option.c: `?`, `??` and unused results (spec 6)
bool option_inner(Checker *c, TypeId type, TypeId *value);   // whether `type` is Option<value>
bool result_parts(Checker *c, TypeId type, TypeId *value, TypeId *error);
TypeId check_try(Checker *c, Expr *e);
TypeId check_coalesce(Checker *c, Expr *e, TypeId expected);
void check_unused_result(Checker *c, const Expr *e);   // E0305
void check_wrapper_layout(Checker *c, DefId id);       // E0806

// check_iter.c: `for x in it` over a type with `fn next(mut self) Option<T>`
bool iterator_item(Checker *c, TypeId type, TypeId *item);
TypeId check_for_iterator(Checker *c, Stmt *s, TypeId item);

// check_clone.c
bool type_is_cloneable(Checker *c, TypeId type);
TypeId check_derived_clone(Checker *c, Expr *call, TypeId self);
void check_std_clone(Checker *c, Expr *call, DefId owner, FnDecl *fn, TypeId self);

// check_user_method.c
// The struct or enum named by `e`, `Point` or `Pair<int, str>`; its type
// arguments go to `args`.
bool type_name_of(Checker *c, const Expr *e, DefId *out, TypeList *args);
bool has_method(Checker *c, DefId owner, Symbol name);   // declared in the type's body
// The type `Name<args>` names, or TID_ERROR for a generic type written
// without arguments, whose arguments are inferred.
TypeId named_instance(Checker *c, DefId id, TypeList args, Span span);
void check_mut_args(Checker *c, const FnDecl *fn, Expr *call);
TypeId check_user_method(Checker *c, Expr *call, DefId owner, bool via_type, TypeHints hints);

// structs.c
StructInfo *struct_fields(Checker *c, DefId id);   // NULL while it is being computed
bool type_is_copy(Checker *c, TypeId type);
TypeId check_struct_field(Checker *c, Expr *e, TypeId base);
TypeId check_tuple_field(Checker *c, Expr *e);
TypeId check_constructor(Checker *c, Expr *call, DefId id, TypeList explicit, TypeId expected);
TypeId check_struct_literal(Checker *c, Expr *e, TypeId expected);
void check_not_recursive(Checker *c, TypeId type, Span span);
void check_lang_layout(Checker *c, const Def *lang, TypeId type);   // E0806

// enums.c
EnumInfo *enum_variants(Checker *c, DefId id);   // NULL while it is being computed
EnumInfo *enum_of(Checker *c, TypeId type, DefId *id);
int64_t variant_index(const EnumInfo *info, Symbol name);
TypeId check_variant_shorthand(Checker *c, Expr *e, TypeId expected);
void unqualified_variant(Checker *c, Span span, DefId id, Symbol name);   // E0326
bool check_qualified_variant(Checker *c, Expr *e, DefId id, TypeHints hints, TypeId *out);
// `Some(x)` (`args` set) or `None` (`args` NULL), named by `def`, a DEF_VARIANT.
TypeId check_prelude_variant(Checker *c, Expr *e, const Def *def, const ArgList *args,
                             TypeId expected);

// moves.c: E0600, E0601, E0602 (spec 5.1); after the function is checked
void check_moves(Checker *c, FnDecl *fn);

// check_match.c
TypeId check_match(Checker *c, Expr *e, TypeId expected, bool as_value);
// exhaustive.c: E0500 for a non-exhaustive match, W0004 for unreachable arms
void check_exhaustive(Checker *c, Expr *e, TypeId type);

// check_pattern.c
// Checks `p` against a value of `type` and gives its bindings their types.
void check_pattern(Checker *c, Pattern *p, TypeId type);
// Whether `p` matches every value of its type (`const` needs such a pattern).
bool pattern_irrefutable(Checker *c, const Pattern *p);

// check_stmt.c
TypeId check_block(Checker *c, Block *block, TypeId expected, bool as_value);
TypeId check_if(Checker *c, Expr *e, TypeId expected, bool as_value);

#endif
