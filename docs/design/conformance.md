# Conformance matrix

What each section of the [specification](../spec/README.md) is implemented
by, and what tests it (OFIR-01 of the
[capability inventory](language-completeness.md), roadmap 6.22a).
`tools/check_conformance.py`, run by `make check-docs`, keeps the matrix in
step with the specification: one row per `## N.M` section, in order, and
every path in the Evidence column exists.

- **Stage 0, Stage 1**: `full` when the compiler implements every rule of
  the section, `partial` when some rules are missing, `none` when it
  implements none, `n/a` when the section states no rule a compiler
  implements. Stage 0 implements the bootstrap subset
  ([bootstrap.md](../bootstrap.md)); stage 1 is meant to implement all.
- **Evidence**: the tests of the section (a directory stands for all its
  tests), or the source that implements it when no test exists.
- **Gaps**: why a stage is `partial` or `none`, and what is untested.
  Each gap starts with its class:

| Class | Meaning |
|-------|---------|
| `subset:` | left out of stage 0 by the bootstrap subset |
| `limit:` | rejected by stage 1 with E0901 (not supported yet) |
| `planned:` | marked **[planned]** in the specification |
| `missing:` | stated by the specification, yet not implemented |
| `untested:` | implemented, with no test of its own |
| `target:` | depends on the platform |

A `missing:` gap is a defect of the implementation or of the
specification; the other classes are known and accepted.

## Chapters 1-4: lexical structure, types, declarations, statements

| Section | Stage 0 | Stage 1 | Evidence | Gaps |
|---------|---------|---------|----------|------|
| 1.1 Source files | full | full | `tests/lex` `tests/lex/columns.of` `tests/compile_fail/lexer/unexpected_character.of` `tests/compile_fail/lexer/invalid_utf8.of` `tests/compile_fail/lexer/bidi_control.of` `tests/lsp/diagnostics.of` | - |
| 1.2 Comments | full | full | `tests/lex/comments.of` `tests/compile_fail/lexer/unterminated_block_comment.of` | - |
| 1.3 Statement termination | full | full | `tests/lex/newlines.of` `tests/parse/generics_and_newlines.of` `tests/compile_fail/parser/semicolon.of` `tests/compile_fail/parser/leading_operator.of` `tests/run/basics/newlines.of` `tests/compile_fail/parser/else_next_line.of` `tests/compile_fail/parser/brace_next_line.of` | - |
| 1.4 Identifiers | full | partial | `tests/lex/keywords_and_names.of` | planned: the naming convention warning (W0007) |
| 1.5 Keywords | full | full | `tests/lex/keywords_and_names.of` | - |
| 1.6 Operators and punctuation | full | full | `tests/lex/operators.of` `tests/parse/expressions.of` | - |
| 1.7 Literals | partial | full | `tests/lex/numbers.of` `tests/run/numbers/bounds.of` `tests/lex/strings.of` `tests/run/strings/literals.of` `tests/run/strings/format.of` `tests/run/strings/utf8.of` `tests/panic/str_boundary_from.of` `tests/compile_fail/types/format_spec.of` `tests/compile_fail/lexer` | subset: format specifiers, raw, multi-line and byte strings |
| 1.8 Documentation comments | full | full | `tests/doc` `tools/check_docs.py` | - |
| 2.1 Primitive types | full | full | `tests/run/basics/arithmetic.of` `tests/run/numbers` `tests/run/numbers/bounds.of` `tests/panic/add_overflow.of` `tests/panic/cast_nan.of` `tests/panic/cast_below.of` `tests/run/numbers/float_edges.of` | planned: `totalCmp` |
| 2.2 The unit type | full | full | `tests/run/basics/control.of` | untested: no test of its own |
| 2.3 Strings | full | full | `tests/run/strings` `tests/panic/str_boundary.of` | - |
| 2.4 Structs | full | full | `tests/run/structs` `tests/compile_fail/types/structs.of` | - |
| 2.5 Enums | full | full | `tests/run/enums` `tests/compile_fail/types/enums.of` | - |
| 2.6 Built-in generic types | full | full | `tests/run/list` `tests/run/map` `tests/run/box` `tests/run/errors` `tests/run/map/custom_keys.of` `std/core/map_test.of` | - |
| 2.7 Fixed-size arrays | partial | partial | `tests/run/arrays` `tests/compile_fail/types/array_not_copy.of` `tests/compile_fail/types/array_length.of` | subset: stage 0 has the type but no array values; limit: arrays of elements that are not Copy |
| 2.8 References | partial | full | `tests/run/refs` `tests/borrow` `tests/compile_fail/types/references.of` | subset: stage 0 checks no borrows (5.4) |
| 2.9 Copy and move types | full | full | `tests/run/moves` `tests/compile_fail/moves.of` | - |
| 2.10 Type equivalence | full | full | `tests/compile_fail/types/mismatch.of` | - |
| 2.11 Tuples | full | full | `tests/run/generics_basic/types.of` `tests/parse/types.of` | untested: no run test of its own |
| 2.12 Slices | partial | full | `tests/run/refs/slices.of` `tests/run/slices/mutable.of` `tests/borrow/mut_slices.of` `tests/panic/slice_range.of` | subset: the exclusive borrow of a `mut` slice |
| 2.13 Function types | none | full | `tests/run/closures/fn_values.of` `tests/compile_fail/types/closures.of` | subset: closures and function values |
| 2.14 Type aliases | partial | full | `tests/run/generics/aliases.of` `tests/compile_fail/types/alias_cycle.of` `tests/compile_fail/types/alias_arity.of` | subset: aliases of type operators are opaque in stage 0 (E0901 on use) |
| 2.15 The never type | full | full | `tests/run/basics/loops.of` `tests/panic/explicit.of` | untested: no test of its own |
| 2.16 Raw pointers | full | full | `tests/run/ffi/libc.of` `tests/compile_fail/types/unsafe.of` | - |
| 2.17 Sized and unsized types | partial | full | `tests/run/dyn` `tests/run/refs/slices.of` | subset: `dyn` (9.7) |
| 3.1 Variables | full | full | `tests/run/basics/constants.of` `tests/compile_fail/types/assignment.of` `tests/compile_fail/names/duplicate_locals.of` | - |
| 3.2 Constants | full | full | `tests/run/basics/constants.of` `tests/compile_fail/types/constants.of` | - |
| 3.3 Functions | partial | full | `tests/run/calls` `tests/compile_fail/types/calls.of` `tests/compile_fail/types/param_defaults.of` `tests/compile_fail/types/named_args.of` | subset: default parameter values and named arguments |
| 3.4 Structs | full | full | `tests/run/structs/values.of` `tests/compile_fail/types/structs.of` | - |
| 3.5 Enums | full | full | `tests/run/enums/values.of` `tests/compile_fail/types/qualified_variants.of` | - |
| 3.6 Methods | full | full | `tests/run/methods` `tests/compile_fail/types/methods.of` `tests/compile_fail/types/user_methods.of` | - |
| 3.7 Destructors | full | full | `tests/run/drops/order.of` `tests/run/drops/unbound.of` | - |
| 3.8 External functions | full | full | `tests/run/ffi/libc.of` `tests/run/ffi/cstring.of` | - |
| 4.1 Blocks | full | full | `tests/run/basics/control.of` `tests/compile_fail/names/scopes.of` | - |
| 4.2 Assignment | full | full | `tests/compile_fail/types/assignment.of` | - |
| 4.3 `if` | full | full | `tests/run/basics/control.of` | - |
| 4.4 `for` | partial | full | `tests/run/basics/loops.of` `tests/run/iterators` `tests/run/loops` `tests/compile_fail/types/labeled_loops.of` `tests/compile_fail/types/for_mut.of` `tests/borrow/for_mut.of` `tests/borrow/drain.of` | subset: labeled loops, and stage 0 finds the iterator method by name, not by interface |
| 4.5 `match` | partial | full | `tests/run/enums/patterns.of` `tests/run/enums/exhaustive.of` `tests/run/enums/at_bindings.of` `tests/compile_fail/types/exhaustive.of` `tests/compile_fail/types/at_bindings.of` | subset: `name @ pattern` |
| 4.6 `if const` and `const ... else` | full | full | `tests/run/enums/if_let.of` | - |
| 4.7 `return`, `break`, `continue` | full | full | `tests/run/basics/loops.of` `tests/compile_fail/types/returns.of` | - |
| 4.8 Operators and precedence | full | full | `tests/run/basics/arithmetic.of` `tests/parse/expressions.of` `tests/compile_fail/types/operators.of` `tests/compile_fail/parser/chained_comparison.of` | - |
| 4.9 Indexing | full | full | `tests/run/list/basics.of` `tests/panic/bounds.of` `tests/panic/bounds_list.of` | - |
| 4.10 Order of evaluation | full | full | `tests/run/basics/eval_order.of` | - |

## Chapters 5-8: ownership, errors, modules, generics

| Section | Stage 0 | Stage 1 | Evidence | Gaps |
|---------|---------|---------|----------|------|
| 5.1 Ownership | full | full | `tests/run/moves` `tests/compile_fail/moves.of` | - |
| 5.2 Destruction (drop) | partial | full | `tests/run/drops` `tests/run/clone/clone.of` `tests/compile_fail/types/clone.of` | subset: stage 0 has no `Clone` bound and clones through the `cloneOf<T>` intrinsic |
| 5.3 Parameter modes | full | full | `tests/run/moves/valid.of` `tests/compile_fail/types/calls.of` | - |
| 5.4 Borrowing rules | none | full | `tests/borrow` | subset: the borrow checker |
| 5.5 Borrowed bindings | partial | full | `tests/borrow/list_iter.of` `tests/run/iterators/list_iter.of` | subset: the borrow checker |
| 5.6 Explicit references (advanced) | partial | full | `tests/borrow/escape.of` `tests/borrow/escape_accepted.of` `tests/compile_fail/types/references.of` | subset: borrows are not checked against moves and mutations, and `ref struct` |
| 5.7 Why this is fast | n/a | n/a | `tests/codegen/noalias.of` | - |
| 5.8 Interior mutability | none | full | `tests/run/cells/cell.of` `tests/compile_fail/types/cell_copy.of` | subset: `Cell<T: Copy>` needs a bound |
| 5.9 Shared ownership | none | partial | `tests/run/rc` `tests/compile_fail/types/rc_send.of` | subset: stage 0 cannot clone a generic struct by method; planned: arena allocators |
| 6.1 `Result` and `Option` | full | full | `tests/run/errors` `tests/run/option` | - |
| 6.2 The `?` operator | partial | full | `tests/run/errors/result.of` `tests/run/errors/try_from.of` `tests/compile_fail/types/errors.of` | subset: error conversion |
| 6.3 Unused results | full | full | `tests/compile_fail/types/errors.of` | - |
| 6.4 Helper methods | partial | full | `tests/run/errors/option.of` `std/core/result_test.of` `std/core/option_test.of` | subset: the helpers that take closures |
| 6.5 Panics | full | full | `tests/panic` | - |
| 6.6 Ending the process | partial | full | `tests/run/system/exit_status.of` `tests/run/system/abort.of` `tests/panic/tasks_at_once.of` | subset: tasks |
| 7.1 Files and modules | full | full | `tests/run/modules/app` | - |
| 7.2 Imports | full | full | `tests/run/modules/app` `tests/compile_fail/warn_imports` `tests/compile_fail/names/assert_import.of` | - |
| 7.3 Visibility | full | full | `tests/compile_fail/modules/visibility` `tests/compile_fail/modules/checks` | - |
| 7.4 The prelude | full | full | `tests/run/basics/prelude.of` | - |
| 7.5 Built-in functions | partial | full | `tests/run/basics/prelude.of` `tests/panic/assert.of` `tests/run/testing/assert_eq.of` | subset: `assertEq` and `assertNe` need interfaces |
| 7.6 Standard library (bootstrap subset) | partial | partial | `std/io/index.of` `std/fs/index.of` `std/os` `std/strings` `std/mem` `std/testing/index.of` | planned: `std.math` and the rest of the standard library |
| 8.1 Generic functions | partial | full | `tests/run/generics_basic/functions.of` `tests/run/generics/bounds.of` `tests/run/generics/bounds_stage0.of` `tests/compile_fail/types/bounds_stage0.of` | subset: bounds other than `Eq`, `Ord` and `Hash` |
| 8.2 Generic types | full | full | `tests/run/generics_basic/types.of` `tests/compile_fail/types/generics.of` | - |
| 8.3 Methods on generic types | full | full | `tests/run/generics_basic/types.of` `tests/run/generics_basic/edges.of` | - |
| 8.4 `where` clauses | partial | full | `tests/run/generics/where.of` | subset: bounds other than `Eq`, `Ord` and `Hash` |
| 8.5 Explicit type arguments | full | full | `tests/run/generics_basic/functions.of` | untested: no test of its own |
| 8.6 Instantiation rules | full | full | `tests/compile_fail/types/generics_depth.of` | - |
| 8.7 Const generics | none | full | `tests/run/generics/const_params.of` `tests/run/generics/length_exprs.of` `tests/compile_fail/types/const_params.of` `tests/compile_fail/types/length_exprs.of` `tests/compile_fail/types/length_mono.of` | subset: const generics |

## Chapters 9-13: interfaces, closures, unsafe, attributes, layout

| Section | Stage 0 | Stage 1 | Evidence | Gaps |
|---------|---------|---------|----------|------|
| 9.1 Declaring interfaces | none | full | `tests/run/interfaces` `tests/compile_fail/types/interfaces.of` | subset: interfaces |
| 9.2 Satisfying interfaces | none | full | `tests/run/interfaces/dispatch.of` `tests/compile_fail/types/interfaces.of` | subset: interfaces |
| 9.3 Default methods | none | full | `tests/run/interfaces/defaults.of` `tests/run/interfaces/embedding.of` `tests/compile_fail/types/interface_conflicts.of` | subset: interfaces |
| 9.4 Embedding | none | partial | `tests/run/interfaces/embedding.of` `tests/compile_fail/types/interface_conflicts.of` | subset: interfaces; limit: embedding a generic interface |
| 9.5 Generic interfaces | none | partial | `tests/run/interfaces/generic.of` `tests/compile_fail/types/generic_interfaces.of` | subset: interfaces; limit: `dyn` of a generic interface |
| 9.6 Static dispatch | none | full | `tests/run/interfaces/params.of` `tests/compile_fail/types/interface_params.of` | subset: interfaces |
| 9.7 Dynamic dispatch with `dyn` | none | partial | `tests/run/dyn` `tests/compile_fail/types/dyn.of` `tests/compile_fail/types/dyn_params.of` | subset: `dyn`; planned: converting a `dyn` value to an embedded interface, `dyn` of a generic interface |
| 9.8 Type switches | none | full | `tests/run/dyn/type_switch.of` `tests/run/dyn/any_generic.of` `tests/compile_fail/types/type_switch.of` | subset: `dyn` |
| 9.9 Built-in interfaces | partial | partial | `tests/run/interfaces/derived.of` `tests/run/strings/debug.of` `tests/compile_fail/types/derived.of` `tests/compile_fail/types/debug.of` `tests/run/map/custom_keys.of` | subset: stage 0 has `Eq`, `Ord` and `Hash` on scalars, `str` and tuples of these only; planned: `Hasher` and a declared `hash`; missing: `cmp` cannot be called on a primitive or on a type parameter bounded by `Ord` |
| 9.10 Coherence | none | full | `compiler/sema/check.of` | subset: interfaces; untested: holds by construction (methods are declared in their type) |
| 10.1 Syntax | none | full | `tests/run/closures` `tests/compile_fail/types/closures.of` | subset: closures |
| 10.2 Two function types | none | full | `tests/run/closures/fn_values.of` `tests/run/closures/fields.of` `tests/compile_fail/types/closure_escape.of` `tests/run/tasks/send.of` | subset: closures |
| 10.3 Captures | none | full | `tests/run/closures/captures.of` `tests/run/closures/move.of` `tests/borrow/closures.of` | subset: closures |
| 10.4 Representation | none | full | `tests/run/closures/move.of` `tests/run/closures/fields.of` | subset: closures |
| 11.1 `unsafe` blocks and functions | partial | partial | `tests/compile_fail/types/unsafe.of` `tests/run/ffi/libc.of` | planned: reading the fields of `union` types |
| 11.2 Raw pointers | full | full | `tests/run/unsafe/ref_to_ptr.of` `tests/run/ffi/libc.of` `tests/compile_fail/types/unsafe.of` | - |
| 11.3 Calling C | full | full | `tests/run/ffi/libc.of` `tests/run/ffi/cstring.of` `tests/run/ffi/c_calls.of` `tests/run/ffi/c_layout.of` `tests/panic/cstring_nul.of` | planned: `@repr(C)` structs and enums by value, `extern "C" fn` types |
| 11.4 Exporting functions to C | none | full | `tests/run/ffi/export_link.of` `tests/codegen/export.of` `tests/compile_fail/attributes/export_link.of` `tests/compile_fail/attributes/reserved_symbols.of` | subset: `@export` |
| 11.5 Mutable globals | partial | partial | `tests/run/globals/app` `tests/compile_fail/types/globals.of` | subset: stage 0 has the declaration but no accesses; limit: globals of types other than numbers and `bool` |
| 11.6 Undefined behavior | n/a | n/a | `tests/panic` `tests/run/unsafe/alignment.of` `tools/check_asan.py` `tools/check_leaks.py` | - |
| 12.1 Attributes | partial | full | `tests/compile_fail/attributes` `tests/run/attributes` `tests/codegen/hints.of` | subset: stage 0 implements `@inline`, `@cold`, `@test` and `@lang` only |
| 12.2 Tests | partial | full | `tests/ofir_test` `tests/run/testing/assert_eq.of` `tests/compile_fail/types/tests.of` | subset: `Result` tests, `assertEq`, `assertNe`, doc tests and concurrent runs |
| 12.3 Benchmarks | none | full | `tests/ofir_bench` `tests/compile_fail/types/benches.of` | subset: `ofir bench` |
| 12.4 Conditional compilation | none | full | `tests/run/attributes/cfg.of` `tests/run/attributes/cfg_windows.of` `tests/compile_fail/attributes/cfg.of` `tests/compile_fail/attributes/variant.of` | subset: `@cfg`; target: `cfg_windows` runs on Windows only |
| 12.5 Documentation comments | partial | full | `tests/ofir_doctest` `tests/doc` `tools/check_docs.py` | subset: doc tests and `ofir doc` |
| 12.6 Warnings | partial | partial | `tests/compile_fail/warn_unused.of` `tests/compile_fail/warn_unreachable.of` `tests/compile_fail/warn_imports` `tests/compile_fail/attributes/deprecated.of` | subset: stage 0 reports W0001, W0002 and W0004 only; planned: unused private items (W0003), needless `let` (W0005) and naming (W0007) |
| 13.1 Primitive types | full | full | `tests/codegen/functions.of` `tests/run/layout/sizes.of` | - |
| 13.2 Structs and tuples | partial | full | `tests/run/layout/field_order.of` `tests/run/layout/repr_order.of` `tests/run/attributes/layout.of` `tests/compile_fail/attributes/layout.of` `tests/run/layout/sizes.of` `tests/run/ffi/c_layout.of` | subset: `@repr(C)`, `@packed` and `@align` |
| 13.3 Enums | full | full | `tests/run/niche` `tests/codegen/niche.of` `tests/run/layout/sizes.of` | planned: compact enum layout |
| 13.4 Built-in types | full | full | `tests/run/niche/niche.of` `tests/run/dyn` `tests/run/layout/sizes.of` | planned: `extern "C" fn` types |
| 13.5 Calling convention | full | full | `tests/codegen/functions.of` `tests/codegen/noalias.of` | - |
| 13.6 Program startup and shutdown | full | full | `runtime/src/entry.c` `tests/run/system/os.of` `tests/panic/stack_overflow.of` | - |
| 13.7 Allocation | full | full | `runtime/src/mem.c` `tests/run/attributes/align_large.of` `tests/run/arena/graph.of` | - |

## Chapters 14-17: concurrency, compile-time code, type operators, unions

Stage 0 implements none of these chapters (bootstrap subset).

| Section | Stage 0 | Stage 1 | Evidence | Gaps |
|---------|---------|---------|----------|------|
| 14.1 Goals | n/a | n/a | | - |
| 14.2 Tasks | none | full | `tests/run/tasks/run.of` `runtime/src/task.c` | subset: tasks |
| 14.3 Marker interfaces | none | full | `tests/run/tasks/send.of` `tests/compile_fail/types/sync.of` | subset: `Send` and `Sync` |
| 14.4 Communication | none | full | `tests/run/sync` `tests/compile_fail/types/channel.of` `tests/panic/waitgroup_below_zero.of` | subset: channels and `std.sync` |
| 14.5 Cancellation, deadlines and shutdown | partial | full | `tests/run/sync/deadlines.of` `tests/run/tasks/shutdown.of` `std/time/index.of` | subset: `std.time` only in stage 0 |
| 15.1 `comptime` blocks and constants | none | full | `tests/run/comptime/eval.of` `tests/run/comptime/consts` `tests/compile_fail/comptime/const.of` `tests/compile_fail/comptime/comptime_fn.of` | subset: compile-time code |
| 15.2 Type information | none | full | `tests/run/comptime/predicates.of` `tests/run/comptime/fields.of` `tests/run/comptime/variants.of` `tests/compile_fail/comptime/fields_of.of` | subset: compile-time code |
| 15.3 `comptime for` and `comptime if` | none | full | `tests/run/comptime/branches.of` `tests/run/comptime/to_json.of` `tests/compile_fail/comptime/conditions.of` | subset: compile-time code |
| 15.4 Attributes as metadata | none | full | `tests/run/comptime/to_json.of` `tests/run/comptime/variants.of` | subset: compile-time code |
| 15.5 Limits | none | full | `tests/compile_fail/comptime/failure.of` `tests/compile_fail/comptime/io.of` `tests/compile_fail/comptime/trace.of` | subset: compile-time code |
| 16.1 Key types and `keyof` | none | full | `tests/run/type_operators/keys.of` `tests/compile_fail/types/key_types.of` `tests/compile_fail/modules/key_visibility` | subset: type operators |
| 16.2 Keys as generic parameters | none | full | `tests/run/type_operators/key_params.of` `tests/compile_fail/types/key_params.of` | subset: type operators |
| 16.3 Indexed access types | none | full | `tests/run/type_operators/keys.of` `tests/compile_fail/types/key_types.of` | subset: type operators |
| 16.4 Mapped types | none | partial | `tests/run/type_operators/mapped_types.of` `tests/run/type_operators/mapped_generic.of` `tests/run/type_operators/mapped_nested.of` `tests/compile_fail/types/mapped_types.of` `tests/compile_fail/types/mapped_unsupported.of` | subset: type operators; limit: a mapped type outside a type alias, or over a type parameter inside another type |
| 16.5 Conditional types | none | full | `tests/run/type_operators/conditional_types.of` `tests/run/type_operators/cond_generic.of` `tests/run/type_operators/recursive.of` `tests/compile_fail/types/conditional_types.of` `tests/compile_fail/types/recursive_types.of` | subset: type operators |
| 16.6 Mapping values | none | full | `tests/run/type_operators/generic_operators.of` `tests/run/type_operators/mapped_generic.of` | subset: type operators |
| 16.7 The standard aliases | none | full | `tests/run/type_operators/core_aliases.of` `tests/compile_fail/types/type_operators_stage0.of` | subset: stage 0 loads the aliases as opaque (E0901 on use) |
| 16.8 Errors | none | full | `tests/compile_fail/types/key_types.of` `tests/compile_fail/types/mapped_types.of` `tests/compile_fail/types/conditional_types.of` `tests/compile_fail/types/recursive_types.of` | subset: type operators |
| 17.1 Union types | none | full | `tests/run/unions/generic.of` `tests/compile_fail/types/unions.of` | subset: union types |
| 17.2 Equivalence | none | full | `tests/run/unions/normalization.of` `tests/compile_fail/types/unions.of` | subset: union types |
| 17.3 Widening | none | full | `tests/run/unions/widening.of` `tests/run/unions/widening_sub.of` | subset: union types |
| 17.4 Narrowing | none | full | `tests/run/unions/narrowing.of` `tests/run/unions/narrowing_drops.of` `tests/compile_fail/types/union_narrowing.of` `tests/compile_fail/types/union_unreachable.of` | subset: union types |
| 17.5 Operations and interfaces | none | full | `tests/run/unions/interfaces.of` `tests/run/unions/try.of` `tests/compile_fail/types/union_interfaces.of` | subset: union types |
| 17.6 Layout | none | full | `tests/run/unions/layout.of` `tests/run/unions/drops.of` | subset: union types |
| 17.7 Errors | none | full | `tests/compile_fail/types/unions.of` `tests/compile_fail/types/union_narrowing.of` | subset: union types |

## Findings

No section has a `missing:` gap.
