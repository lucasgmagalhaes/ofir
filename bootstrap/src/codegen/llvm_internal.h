// State shared by the LLVM IR generator files.
//
// Each AIR local is an `alloca` in the entry block (`%l<N>`); LLVM's mem2reg
// promotes them to registers. SSA temporaries are `%t<N>`. String literals,
// source locations and intrinsic declarations are collected while the
// functions are generated and written after them.

#ifndef OFIR_LLVM_INTERNAL_H
#define OFIR_LLVM_INTERNAL_H

#include "codegen/llvm.h"
#include "codegen/out.h"

struct Gen {
    Sema *sema;
    TypeTable *types;        // instances add the types of their fields
    Out out;
    Out globals;             // string literals and locations, written at the end
    uint32_t str_count, loc_count;
    uint64_t overflow_used[2][3][4];   // [signed][add, sub, mul][i8, i16, i32, i64]
    bool float_fn_used[FF_COUNT][2];   // [function][f32, f64]
    const AirBody *body;     // the function being generated
    uint32_t temp;           // next %t number in this function
    Out path;                // the escaped source path of the root module
    Vec(bool) files_emitted; // by FileId: whether @.file.<id> exists
    Out type_defs;           // struct type definitions not written yet
    Out fn_text;             // the function being generated (llvm_function)
    const char **adt_types;  // the LLVM name of each struct or enum, by TypeId (NULL until used)
    const uint32_t **slots;  // by TypeId: the slot of each field of a struct (field_slots)
    size_t adt_count;        // the length of both tables
    Vec(bool) c_used;        // by DefId: functions of extern blocks to declare
    Vec(bool) globals_used;  // by DefId: mutable globals to define
    TypeId niche_tag;        // the enum whose tag the last place_address named...
    bool has_niche_tag;      // ...when it has a niche (spec 13.3)
};

// A generated value: its LLVM type and its text (`%t3`, `42`, `true`, ...).
typedef struct {
    const char *type;
    char text[160];
} Value;

const char *llvm_type(Gen *g, TypeId type);
bool llvm_has_value(TypeId type);   // false for () and !
StrView def_name(Gen *g, DefId def);
void function_name(Gen *g, Out *out, DefId def, TypeId targs, bool is_main);
bool is_c_function(const Def *def);   // in an extern block, or `extern "C" fn`
void mark_c_used(Gen *g, DefId def);   // declare it at the end of the module

// A fresh temporary name, written into `text`.
void next_temp(Gen *g, char text[16]);

// llvm_types.c
// The address of a place: `%l<N>` itself, or a getelementptr to the field.
void place_address(Gen *g, LocalId local, FieldPath path, char text[16]);
const char *adt_type(Gen *g, TypeId type);          // defined at its first use
const char *variant_type(Gen *g, TypeId adt, uint32_t v);
// The variant of a niche enum that holds a pointer, or -1 (spec 13.3).
int niche_variant(Gen *g, TypeId adt);
// The slot of each field of a struct in its LLVM type (spec 13.2).
const uint32_t *field_slots(Gen *g, TypeId type);
// A struct or tuple value: insertvalue of each field into poison.
void gen_aggregate(Gen *g, const Rvalue *rv, TypeId type, Value *result);
// Reads and writes the tag of the niche enum `g->niche_tag` at `at`.
void niche_tag_value(Gen *g, const char *at, Value *v);
void niche_tag_store(Gen *g, const char *at, Value v);
// `<int, str>`: the type arguments of an instance, for its name.
void type_args_text(Gen *g, Out *out, TypeId targs);
const char *tuple_type(Gen *g, const Ty *ty);

uint32_t string_global(Gen *g, StrView text);   // the N of @.str.N
uint32_t location_global(Gen *g, Span span);     // the N of @.loc.N

// llvm_body.c
void gen_body(Gen *g, const AirBody *body);

// llvm_ops.c
Value gen_operand(Gen *g, Operand o);
void gen_rvalue(Gen *g, const Rvalue *rv, TypeId dest_type, Value *result);
void gen_mut_globals(Gen *g);   // the definitions of the mutable globals used
void gen_cast_fails(Gen *g, const Rvalue *rv, Value *result);
void gen_saturate(Gen *g, const Rvalue *rv, Value *result);
void gen_cast_value(Gen *g, TypeId from, TypeId to, Value a, Value *result);
void gen_misc(Gen *g, const Rvalue *rv, Value *result);   // select, str, float functions
void gen_atomic(Gen *g, const Rvalue *rv, Value *result);   // llvm_atomic.c
void gen_float_decls(Gen *g);

#endif
