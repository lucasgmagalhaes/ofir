# Roadmap

This roadmap turns the specification and design documents into an ordered
list of small, verifiable tasks. Each task should fit in one pull request
made of small Conventional Commits, and is done only when its tests pass in
CI.

## Strategy

1. **Validate before building.** Before writing stage 0, part of the future
   compiler is written on paper in the bootstrap subset, to confirm the
   subset (and second-class references) are expressive enough.
2. **Walking skeleton first.** Get `hello world` working end to end through
   every phase (lexer → clang) as early as possible, then grow the language
   one feature at a time *through all phases*.
3. **Tests before features.** Each feature starts with its `tests/run` and
   `tests/compile_fail` cases.
4. **Stage 0 stays minimal.** Anything not needed to build the self-hosted
   compiler goes to stage 1.
5. **Port, then extend.** Stage 1 first reproduces stage 0 (same test
   results), then gains the borrow checker (required for M4), then the full
   language.

Size legend: **S** ≈ a few hours, **M** ≈ a few days, **L** ≈ one to two
weeks of focused work.

## Overview

```
Phase 0  Foundations            decisions, repository, CI, test runner, paper prototype
Phase 1  Walking skeleton       hello world through all phases           → M1
Phase 2  Stage 0 core language  numbers → structs → enums → collections
Phase 3  Stage 0 systems        ownership, modules, FFI, std subset      → M2
Phase 4  Self-hosting           compiler in Ofir + borrow checker       → M3, M4
Phase 5  Full language          generics, interfaces, closures, perf     → M5
Phase 6  Towards 1.0            concurrency, tools, packages, docs       → v1.0
```

## Decisions needed before Phase 0

| Decision | Blocks | Status |
|----------|--------|--------|
| License | 0.2 | decided: **MIT** |

Everything else needed for Phases 0–3 is decided in the specification and
the ADRs.

---

## Phase 0 — Foundations

| # | Task | Size | Done when |
|---|------|:----:|-----------|
| 0.1 | Create the directory skeleton of `docs/design/project-structure.md` with a `README.md` per top-level directory | S | **done**: directories exist and describe their purpose |
| 0.2 | Add `LICENSE`, `CONTRIBUTING.md` (Conventional Commits, branches, PRs, ADR process), `CHANGELOG.md`, `.editorconfig`, `.gitignore` | S | **done**: files present |
| 0.3 | Top-level `Makefile` with targets `stage0`, `runtime`, `test`, `clean` (empty implementations allowed) | S | **done**: `make` runs without errors |
| 0.4 | `bootstrap/Makefile`: C11, `-Wall -Wextra -Werror`, debug build with `-fsanitize=address,undefined` | S | **done**: an empty `main.c` builds |
| 0.5 | Test runner `tests/runner/run_tests.py`: discovery, `run`, `compile_fail` (stage 0 and stage 1 matching rules), `panic` suites, `// requires:` tags, `--bless`, parallel execution | M | **done**: runner self-test passes on fixture files |
| 0.6 | Script checking that every error code used in tests exists in `docs/errors/README.md` | S | **done**: runs in CI |
| 0.7 | CI workflow: build stage 0 and runtime, run all suites on Linux and macOS with the oldest and newest supported LLVM | S | **done**: green pipeline on the main branch |
| 0.8 | **Paper prototype:** write the stage 1 lexer (`compiler/syntax/lexer.of`, ~500 lines) in the bootstrap subset, without compiling it; record every missing feature or awkward pattern | M | done: see [prototype review](design/prototype-review.md) |

## Phase 1 — Walking skeleton (milestone M1)

Goal: `ofir run examples/hello.of` prints `Hello, Ofir!`.

| # | Task | Size | Done when |
|---|------|:----:|-----------|
| 1.1 | `base/`: arena allocator, dynamic array macros, string builder, string views | M | **done**: C unit tests pass |
| 1.2 | `base/`: hash map (insertion ordered) and identifier interning | S | **done** (interning; the general map is added when first needed): C unit tests pass |
| 1.3 | `base/`: file reading, path joining; file table with `FileId` and spans | S | **done**: C unit tests pass |
| 1.4 | `diag/`: diagnostics with codes, rendering with source excerpt and caret | S | **done**: output matches the design doc format |
| 1.5 | Lexer: all tokens of spec chapter 1 except interpolation, raw and byte strings; newline tokens; contextual keywords as identifiers | M | **done**: `tests/lex` cases pass, `--emit=tokens` works |
| 1.6 | Lexer: string interpolation tokens | S | **done**: `tests/lex/interpolation*` pass |
| 1.7 | AST definitions and AST printer (`--emit=ast`) | M | **done**: printer covers every node kind |
| 1.8 | Parser: items (`fn`, `struct`, `enum`, `const`, `import`), types, statements, parser-driven newline handling (spec 1.3) | M | **done**: `tests/parse` cases pass, including `tests/parse/generics_and_newlines` |
| 1.9 | Parser: expressions with precedence, struct literal restriction, generic disambiguation, patterns | M | **done**: `tests/parse` cases pass |
| 1.10 | Parser: error recovery to next statement | S | **done**: several syntax errors reported per file (`tests/compile_fail/parser/recovery`) |
| 1.11 | Types: interned type table with primitive types, `str`, `()` | S | **done**: unit tests (`bootstrap/tests/test_types.c`) |
| 1.12 | Resolver: single module, functions and locals | S | **done**: `E0200`/`E0201` reported (`tests/compile_fail/names`) |
| 1.13 | Type checker: functions, `const`/`let`, integer literals, calls, `println(str)`, literals passed to `str` parameters, interpolation of `str` and `int` | M | **done**: `tests/compile_fail/types/*` pass, also in stage 1 mode |
| 1.14 | AIR definitions, printer (`--emit=air`) and validator | M | **done**: AIR dumps for hello world |
| 1.15 | Lowering for the skeleton subset | S | **done**: AIR validator passes |
| 1.16 | Codegen: functions, locals, integer arithmetic, calls, string constants; no `noalias` | M | **done**: generated `.ll` accepted by clang (CI: 17 and the latest) |
| 1.17 | Runtime: entry point, stdout buffer, `ofir_write`, panic functions, allocator | S | **done**: runtime builds with `-Werror` |
| 1.17a | **done**: Benchmark harness: `bench/run_bench.py`, `--time-passes`, `--stop-after`, CI regression gate | S | `make bench` and `make bench-compare` work |
| 1.17b | Benchmarks for each new phase (`parse-100k`, `check-100k`, `build-100k`) added with the phase | S | **done**: `parse-*`, `check-*` and `llvm-*` (IR generation without clang) |
| 1.18 | Driver: `ofir build`/`run` for a single file, invoking clang, `--emit=*` flags | S | **done — M1: hello world runs**; binary size: 17 KiB (14.6 KiB stripped) |

## Phase 2 — Stage 0 core language

Each task covers lexer → parser → checker → lowering → codegen → tests.

| # | Task | Size | Done when |
|---|------|:----:|-----------|
| 2.1 | All integer types, `float`, `bool`; `as` conversions with checks; `wrappingCast`/`saturatingCast`/`checkedCast` | M | **done**: `tests/run/numbers/*`, `tests/panic/cast*` pass; `checkedCast` since 2.22 |
| 2.2 | Checked arithmetic (`llvm.*.with.overflow`), division semantics, shifts | S | **done**: `tests/panic/*overflow*`, `div_*`, `shift` pass |
| 2.3 | Comparison, logic (short-circuit), bitwise operators | S | **done**: covered by `tests/run/basics/*` |
| 2.4 | `if`/`else` as statement and expression | S | **done**: `tests/run/basics/control` |
| 2.5 | `for` (infinite, condition, ranges), `break`, `continue` | S | **done**: `examples/fibonacci.of` runs |
| 2.6 | Missing-return analysis, `!` type, `panic`, `assert` | S | **done**: `tests/compile_fail/types/returns`, `tests/panic/assert`, `explicit` |
| 2.7 | `const` declarations with compile-time evaluation | S | **done**: `tests/run/basics/constants`, `tests/compile_fail/types/constants` |
| 2.8 | `str`: literals, `+`, `+=`, `==`, comparison, `len`, byte indexing, `push` | M | **done**: `tests/run/strings/*` pass |
| 2.9 | Interpolation of all primitive types; shortest round-trip float formatting (spec 2.1) | M | **done**: `tests/run/basics/strings`, `numbers/methods` |
| 2.10 | Structs: declaration, literals, field access and assignment, nested structs, `copy struct`, default field values, tuple structs | M | **done**: `tests/run/structs/*` pass |
| 2.11 | Methods and associated functions declared inside structs and enums, `self` | M | **done** for structs (enums with 2.14): `tests/run/methods/*` pass |
| 2.12 | Parameter modes (borrow, `mut`, `move`) for parameters and `self`, `for mut`, `[T]`/`str` view parameters; explicit `&T`/`&mut T` locals, auto-deref, `*` | M | **in progress**: modes of parameters and `self` done (`mut` by reference); `for mut` done; views, `&T` locals and `*` remain |
| 2.13 | Tuples and destructuring `const` | S | **done**: `tests/run/enums/patterns` |
| 2.14 | Enums: layout, construction, `.Variant` shorthand | M | **done**: `tests/run/enums/*` |
| 2.15 | `match`: decision trees, literal, range, variant, tuple, struct patterns, guards; Copy bindings through references | L | **done** with sequential arm tests (decision trees later, if profiles ask for them); `examples/shapes.of` runs since `List` (2.20) |
| 2.16 | Exhaustiveness checking (usefulness algorithm) | M | **done**: `sema/exhaustive.c`; errors show an uncovered value (`Err(.TooLarge)`), unreachable arms warn (W0004); `tests/compile_fail/types/exhaustive.of`, `tests/run/enums/exhaustive.of` |
| 2.17 | `if const`, `const ... else`, `??` | S | **done**: `tests/run/enums/if_let`; `??` since 2.22 (`tests/run/errors/option.of`) |
| 2.18 | Generic functions, structs and enums **without bounds**; monomorphization and mangling | L | **done**: `tests/run/generics_basic/*`, `tests/compile_fail/types/generics*`; type arguments are inferred from the receiver, the expected type and the arguments |
| 2.19 | `Box<T>` and recursive enums | M | **done**: `tests/run/box/expr_tree.of`; `Box` is written in `std/core/box.of` with `@lang("box")` and the `sizeOf<T>()` intrinsic; a box is freed by `unbox` until drops (3.2) |
| 2.20 | `List<T>` (std/collections.md S0 entries): literals, bounds checks, `for` over lists with Copy loop variables, `sort` | L | **done**: `tests/run/list/*`, `tests/panic/bounds_list*`; written in `std/core/list.of` (`@lang("list")`); `pop`, `get`, `first`, `last` wait for `Option` and borrows (2.21, 2.22), `contains` and `join` for bounds; `sort` takes int, float and str |
| 2.21 | Slices `&[T]` and `&str`, range indexing, built-in borrow-returning methods (`get`, `first`, `last`, `getMut`) | M | **done**: `tests/run/refs/*`, `tests/compile_fail/types/references.of`; `&T`, `&mut T` and the second-class rules (E0702); `Slice<T>` in `std/core/slice.of`; borrows are not checked until the borrow checker (4.7); `&str` is a `str` view, `&mut [T]` and `Option.asRef` wait |
| 2.22 | `Option<T>`, `Result<T, E>`, `?` (equal error types), `unwrap`, `expect`, unused-`Result` error | M | **done**: `tests/run/errors/*`; written in `std/core/option.of` and `result.of` (`@lang("option")`, `@lang("result")`); `Some`, `None`, `Ok`, `Err` in scope everywhere; `List.pop`; `asRef` waits for references (2.21) |
| 2.23 | `Map<K, V>`: insertion order, deterministic hashing of integers, `str` and tuples | L | **done**: `tests/run/map/*`; written in `std/core/map.of` and `hash.of`, keys hashed through the `hashOf`/`equals` intrinsics; `for key, value in map` and `for i, x in list` |
| 2.24 | Warnings: unused variables, unused imports, unreachable code; `--deny-warnings` | S | **done**: `tests/compile_fail/warn_*`; W0001 exempts parameters and names starting with `_`; `run` tests use `--deny-warnings` |

## Phase 2b — Standard library in Ofir (ADR 0019)

Done before the rest of phase 2, so that library functions are written in
Ofir instead of being built into the compiler.

| # | Task | Size | Done when |
|---|------|:----:|-----------|
| 2b.1 | Modules (task 3.4): several files, `import`, aliases, `pub`, qualified names | L | **done**: `tests/run/modules/*` |
| 2b.2 | Prelude: `std/core` loaded in every compilation, its `pub` names visible everywhere (task 3.5) | M | **done**: `Ordering` in `std/core/index.of` |
| 2b.3 | `extern "C"`, `unsafe` blocks and raw pointers `*T` (task 3.6) | M | **done**: `tests/run/ffi/libc.of` |
| 2b.4 | `@lang` declarations of primitive types and `extern "intrinsic"` functions | M | **done**: `std/core/str.of` |
| 2b.5 | Move the built-in methods of `str`, integers and floats to `std/core`; remove `sema/builtins.c`; shrink the runtime to entry, panics, allocation and I/O | L | **done**: only `wrappingCast`, `saturatingCast`, `p.add` and the numeric constants stay built in until generic bounds (5.2); the integer methods are generated by `tools/gen_core_numbers.py` |

Then phase 2 resumes with generics (2.18, done), so that `Box`, `List`,
`Option` and `Result` are written in `std` from the start (task 5.6 moves
earlier accordingly).

## Phase 3 — Stage 0 systems features (milestone M2)

| # | Task | Size | Done when |
|---|------|:----:|-----------|
| 3.1 | Move analysis on AIR: use after move, conditional moves, moves out of borrowed or indexed places, definite initialization | L | **done** on the typed AST (`sema/moves*.c`): `tests/compile_fail/moves.of`, `tests/run/moves/*`; `std.mem.replace` and `swap`; Map removes entries instead of marking them dead |
| 3.2 | Drop elaboration, drop flags, drop glue, user `drop` methods | L | **done**: `make check-leaks` (valgrind, as clang here has no ASan runtime) finds no leak or invalid access in `tests/run` and `examples`; `tests/run/drops/order.of`; the unbound parts of a value matched by move are not dropped yet |
| 3.3 | `.clone()` for built-in types and derived for structs/enums | M | **done**: clone glue `clone<T>` (`air/lower_clone.c`), the derived clone and E0322 (`sema/check_clone.c`), `clone` in std str, List and Box; `tests/run/clone`, `tests/compile_fail/types/clone.of` |
| 3.4 | Modules: multiple files, `index.of`, `import`, aliases, `pub`, module-qualified names | L | multi-file tests pass |
| 3.5 | Prelude and standard library loading from `std/` | M | `Option`/`Result` helpers live in `std/core` |
| 3.6 | `extern "C"` blocks, `unsafe` blocks, raw pointers | M | calling `strlen` works |
| 3.7 | `std.strings` and `str` S0 methods, including the borrowing iterators `split`, `splitWhitespace`, `lines`, `bytes` | M | **done**: `for x in it` over any type with `fn next(mut self) Option<T>` (`sema/check_iter.c`, `air/lower_iter.c`); `find`, `trim*`, `replace`, `pushChar`, `withCapacity` in `std/core/str.of`, the iterators in `std/core/split.of`; `std.strings` `join`, `fromUtf8`, `Builder`; `tests/run/strings/*`, `tests/run/iterators/*` |
| 3.8 | `std.io` and `std.fs` S0 entries, `IoError` | M | **done**: `examples/word_count.of` runs; runtime `fs.c`, `std/io`, `std/fs`, `eprint`/`eprintln`; `tests/run/system/fs.of`; `main` still returns nothing |
| 3.9 | `std.os` S0 entries: args, env, exit, `run` | M | **done**: `tests/run/system/os.of` runs `clang --version`; runtime `os.c` (`posix_spawnp`, both streams captured) |
| 3.10 | Runtime: stack overflow detection | S | **done**: `runtime/src/stack.c` (`sigaltstack` + `SIGSEGV`/`SIGBUS` handler); `tests/panic/stack_overflow*` pass |
| 3.11 | `@test` functions and `ofir test` | M | **done**: `ofir test` (`air/lower_test.c`, `driver_test.c`), one process per test; `make test-std` (`tools/run_std_tests.py`) in `make test`; `tests/ofir_test/` |
| 3.11a | First generated-code benchmarks (`fib`, `nbody`, `binary_trees`) against C | M | **done**: `bench/run_programs.py`; `binary_trees` 1.01, `nbody` 1.04, `fib` 1.80 (overflow checks; 1.0 with wrapping arithmetic), in `bench/HISTORY.md` |
| 3.12 | Freeze review: all suites under ASan/UBSan, fix leaks and crashes | M | **done**: `make check-asan` (69 programs clean), `make check-leaks` (47 clean), `tools/fuzz_frontend.py` (2 600 mutated inputs, no crash); **M2: stage 0 complete** → release 0.1 |

## Phase 4 — Self-hosting (milestones M3 and M4)

Stage 1 is written in the bootstrap subset and mirrors stage 0's structure.
Each port is verified by comparing outputs with stage 0.

| # | Task | Size | Done when |
|---|------|:----:|-----------|
| 4.1 | `compiler/base`: generic typed index vectors, interning, spans | M | **done**: `base.span` (spans, `SourceMap`, line/column), `base.intern`, `base.index_vec` (`Id<T>`, `IndexVec<T>`), 8 tests in `make test-std`; `compiler/ofir.json` makes `compiler/` the import root |
| 4.2 | `compiler/diag` | S | **done**: `diag` (`Diagnostics`, human and short formats, sorted and deduplicated as stage 0 does); tests compare with stage 0's output |
| 4.3 | `compiler/syntax/lexer` (starting from the 0.8 prototype) | M | **done**: `main tokens <file>` identical to stage 0's `--emit=tokens` (output, errors, status) on every `.of` file of the repository and 200 mutated ones (`make compare-stages`, `tools/compare_stages.py`); `checkedAdd/Sub/Mul/Div` and `parse` for the numeric types in std |
| 4.4 | `compiler/syntax/parser` + AST printer | L | **done (M3)**: `main ast <file>` identical to stage 0's `--emit=ast` on every `.of` file of the repository, `compiler/` included, and on mutated sources (`make compare-stages`); found and fixed in stage 0: a double free when matching a value in a box, a partial move kept across loop iterations, a crash printing a tree after errors |
| 4.5 | Resolver and module loading | M | **done**: `compiler/driver/modules` loads the program as stage 0 does, `compiler/sema/resolve*` binds every name; `main names <file>` equals stage 0's new `--emit=names` (what each name resolves to, the locals of each function) with the same errors and warnings on every file of the repository and on mutated sources; the AST numbers its nodes (`NodeId`) so that results are kept beside it; `fs.canonicalize` |
| 4.6 | Type checker | L | **done**: `compiler/sema/check*` and the files beside it; `main types <file>` equals stage 0's new `--emit=types` (signatures, fields, variants, constant values, the type of every local) with the same errors and warnings on every file of the repository, `tests/compile_fail` included, and on mutated sources; the implicit dereferences, conversions and type arguments that stage 0 writes into the tree are tables beside it; found and fixed in stage 0: a reference to a `match` binding could be returned, and an unknown module in a type path (`nowhere.T`) was silently an error type |
| 4.7 | AIR, lowering, move analysis, drop elaboration, monomorphization | L | **done**: `compiler/sema/moves*.of`, `compiler/air/`; `main air` equals stage 0's `--emit=air` on every program of the repository (`make compare-stages`), stage 1 itself included (154,510 lines of AIR); the move errors are compared through `--emit=types`; the entry point of `ofir test` is lowered with the driver (4.9) |
| 4.8 | LLVM codegen and layout | L | **done**: `compiler/codegen/llvm.of`; `main llvm` equals stage 0's `--emit=llvm` on every program of the repository (`make compare-stages`); `main build` and `main run` link with clang, and `tests/run` and `tests/panic` pass with stage 1 built by stage 0 (`make test-stage1`, 67 tests) |
| 4.9 | Driver and CLI | M | **done**: `compiler/main.of` has stage 0's command line; `ofir test` loads the test files; every test passes with stage 1 as the compiler (`make test-stage1`: 130 runner tests, the 68 `@test` functions of std and of the compiler), and `make compare-stages` compares the stages through `ofir build` and `ofir test` |
| 4.10 | Borrow checker: loans, liveness, conflicts (spec 5.5) | L | **done**: `compiler/sema/borrows.of`, in the move analysis walk; `tests/borrow/*` pass with stage 1 (E0700, E0701, E0703-E0705); non-lexical borrows, per-field places, two-phase borrows, borrows returned through a parameter; fixed what it found: one error in `compiler/`, and a std test that swapped two elements of one list |
| 4.11 | Escape rules for references and borrowed function values (spec 5.4, 10.2) | M | **done**: stage 1 reports E0702 for a borrowed `fn(A) R` stored in a field, variant or container, or returned, and E0706 for a borrow that outlives the block of what it borrows (block ends, `break`, `continue`, loop iterations, block values); `tests/borrow/escape*.of`; no errors in `compiler/` or std |
| 4.12 | Fix the compiler's own code until it passes its borrow checker | M | **done**: `compiler/` compiles with no borrow errors; the one error found (`checkLate` in `compiler/sema/check.of`) was fixed with 4.10, and the escape rules of 4.11 found none; `make verify` builds stage 2 and stage 3 with the checker on |
| 4.13 | Stage 1 builds itself (stage 2), stage 2 builds itself (stage 3) | M | **done** (before 4.10–4.12, which it does not need): `make verify` (part of `make test`) builds stage 2 and stage 3; their LLVM IR for `compiler/` is identical, and equal to stage 0's |
| 4.14 | `make verify` in CI; publish the first seed (bootstrap.md) | S | **done**, **M4: self-hosted** → release 0.2: `make verify` runs in `make test` on CI; `make seed`, `make check-seed`, and the Release workflow, which publishes `ofir-0.2.0.ll` with its checksum (`seed/SEEDS.md`) |

From M4 on, new features are implemented only in stage 1. Stage 0 is
frozen. `compiler/` may use a feature only once it has shipped in a tagged
release (bootstrap chain, `docs/bootstrap.md`).

## Phase 5 — Full language (milestone M5)

| # | Task | Size | Done when |
|---|------|:----:|-----------|
| 5.1 | Returning borrows and `ref struct` (spec 5.6) | M | **done**: `List.iter()` written in Ofir |
| 5.2 | Generic bounds and `where` clauses | M | **done**: the built-in `Eq`, `Ord`, `Hash` and `Clone` until interfaces (5.3); `tests/run/generics/*` pass; E0332-E0334 tested |
| 5.3 | Interfaces: declaration, structural satisfaction, assertions, default methods, embedding | L | **done**: static dispatch per instance; generic interfaces wait (E0901); `tests/run/interfaces/*` pass; E0332, E0333 and E0335 tested |
| 5.4 | `dyn`: vtables, fat pointers, unsized types (spec 9.7) | M | **done**: `dyn I` parameters, `&dyn I`, `*dyn I` and `Box<dyn I>`, with calls and drop through the vtable; `mut dyn I` parameters wait (E0901); `tests/run/dyn/*` pass; E0338, E0339 and E0355 tested |
| 5.4b | Type switches, `Any` and `typeName` (spec 9.8, ADR 0024) | M | **done**: `examples/interfaces.of` runs; type tests on `&dyn I`, `*dyn I` and `Box<dyn I>`, in `match` and `if const`; `tests/run/dyn/type_switch.of` passes; E0354 tested |
| 5.4c | Interfaces as parameter types: anonymous type parameters (spec 9.6, ADR 0029) | S | **done**: `tests/run/interfaces/params.of` covers `s: I`, `mut I`, `&I`, `[I]`, `List<I>` and `Option<I>` parameters; `tests/compile_fail/types/interface_params.of` tests E0356 |
| 5.5 | Built-in interfaces derived automatically (`Eq`, `Ord`, `Hash`, `Clone`, `Debug`); `From` in `?` | M | `==` on structs, structs as map keys; **done**: `Eq`, `Ord`, `Hash` derived in stage 1 (`compiler/sema/derive.of`, `compiler/air/lower_derive.of`: glue `eq`, `hash`, `cmp`), declared `eq`/`cmp` honored, stage 0 rejects them as a stage 0 limit (E0901); `Clone` was done in 3.3; `?` converts the error with the function error type's `fn from(value: move E) Self` (stage 1; `From<T>` itself waits for generic interfaces); `Debug` derived in stage 1 (glue `debug`, declared `debug` honored, `List`/`Box`/`Map` declare it in std, runtime `ofir_str_push_debug`, E0314) |
| 5.6 | Rewrite `List`, `Map`, `Option`, `Result`, `Box` as generic Ofir code in `std/` | L | built-in special cases removed from the compiler; **done**: the types are Ofir code in `std/core`; stage 0 checks the bounds `Eq`, `Ord` and `Hash` (also in `where` and on methods), so `Map<K: Eq + Hash, V>` and `List.sort() where T: Ord` use `==`, `<` and `hashOf` instead of the `equals` and `lessThan` intrinsics and the checkers' `Map` key and `sort` special cases (E0332 in both stages); `tests/run/generics/bounds_stage0.of` passes |
| 5.7 | Closures: arrow syntax (`x => ...`, `move () => ...`, ADR 0023), `fn` and `move fn` types, captures, stack and heap environments | L | **done**: `mut` and `move` closure parameters wait (E0901); `tests/run/closures/*` pass; E0336, E0337 and the capture borrows tested |
| 5.8 | Iterator interface and `for` over iterators | M | **done** (stage 1): generic interfaces (bounds, assertions, inference from the bounded type), `Iterator<T>` in the prelude, `for` over bounded parameters; `dyn` of generic interfaces and embedding them are not supported yet (E0901) |
| 5.9 | Interior mutability: `UnsafeCell`, `Cell` | S | **done** (stage 1): `std/mem/cell.of`, the `Copy` bound, borrowed values with interior mutability passed and bound by address; `tests/run/cells/cell.of` passes; E0332 with `Copy` tested |
| 5.10 | Format specifiers, raw strings, multi-line strings, byte strings | M | tests pass; **done** in stage 1: raw, multi-line and byte strings (`compiler/syntax/literals.of`, E0007), format specifiers (`compiler/syntax/format_spec.of`, runtime `ofir_str_push_fixed`/`radix`/`padded`, E0357) and `?` with `Debug` (5.5) |
| 5.11a | Attributes checked in both stages (E0807-E0810: name, target, arguments, duplicates); `@inline`, `@inline(never)` and `@cold` become LLVM function attributes | S | **done**: `tests/compile_fail/attributes/*` and `tests/codegen/hints.of` pass in both stages; the other attributes report E0901 |
| 5.11b | `@deprecated` (W0006) and `@allow(category)` | S | **done** (stage 1): W0006 on uses of deprecated functions, methods, types, constants and globals (not fields); `@allow(unused\|unreachable\|deprecated\|naming)` silences warnings inside its declaration; `tests/compile_fail/attributes/{deprecated,allow}.of` pass; stage 0 accepts both and ignores them |
| 5.11c | `@cfg` conditional compilation (spec 12.4) | S | **done** (stage 1): `os`, `arch`, `debug`, `test` with `&&`, `\|\|`, `!`; excluded declarations, imports, fields, methods and extern functions are removed before resolution (`compiler/driver/cfg.of`, `os.name`/`os.arch`); `tests/run/attributes/cfg.of` passes; E0809 tested |
| 5.11d | `@repr(C)`, `@packed`, `@align(N)` | M | **done** (stage 1): `@repr(C)` keeps declaration order and applies to enums without payloads; `@packed` structs are LLVM packed types read and written with `align 1`, their fields `bool`, numbers or `@packed` structs (E0811) and never borrowed (E0812); `@align(N)`, N up to 4096 (E0809 above), adds an empty aligned field that enum payloads follow; `Box`, `List` and `move` closures allocate types aligned above 16 with `ofir_alloc_aligned`, through the `alignOf<T>()` intrinsic; `tests/run/attributes/layout.of` and `align_large.of` pass |
| 5.11e | `@export`, `@link` | S | **done** (stage 1): `@export("name")` names the C symbol of an `extern "C" fn` (a C identifier; E0808 on other functions) and E0813 rejects a symbol defined twice; `@link("name")` passes `-lname` to clang once per library; `tests/run/ffi/export_link.of` and `tests/codegen/export.of` pass |
| 5.12a | Default parameter values and named arguments | M | **done** (stage 1): defaults evaluated at each call that omits them, in the callee's module, after the written arguments; named arguments on functions, methods and `dyn` calls; defaults of generic-typed parameters wait (E0901); `tests/run/calls/*` pass; E0302, E0306, E0308, E0358 and E0359 tested |
| 5.12b | Diagnostics as JSON: `--message-format=json` | S | **done** (stage 1): one object per line with severity, code, message and the span's file, line, column, end line and end column (`docs/design/toolchain.md`); driver errors without a location; unit tests in `compiler/diag/index_test.of` |
| 5.12c | Diagnostics: notes and help messages | M | **done** (both stages): `= note:` and `= help:` lines after the excerpt, a `notes` array in JSON, left out of the short format (`docs/design/toolchain.md`); E0600 and E0304 suggest their fix |
| 5.12e | Diagnostics: "did you mean" for unknown names, methods and fields (E0200, E0301, E0316) | S | **done** (both stages): an unknown name, import, type, field or method gets `= help: did you mean `x`?` with the closest candidate by edit distance (with swaps), at most a third of the name's length |
| 5.12d | `ofir explain` and an explanation file for every registry code | M | **done** (stage 1): `ofir explain` prints `docs/errors/EXXXX.md`; every code that is not planned has a file, and `make check-codes` compiles each example (it must report its code) and fix (it must compile cleanly) with stage 1 |
| 5.13 | Debug info (DWARF) | M | **done**: stage 1 `-g` emits functions and line tables; breakpoints on functions and lines, `next` and `step` work in `lldb` and `gdb` |
| 5.14 | Optimization: `noalias`/`readonly` (stage 1 only), niche layouts, field reordering, AIR check elimination | M | done: stage 1 `--release` emits `noalias`/`readonly` parameters; niche enums (`Option<&T>`, `Option<Box<T>>`, `Option<fn>`) keep no tag, struct fields are ordered by descending alignment, and checks with a condition known while compiling are folded, in both stages; LLVM already drops the bounds checks of index loops, and fib keeps the overflow checks its semantics require (bench/HISTORY.md) |
| 5.14e | Optimization: accumulator recursion (`return x + f(args)` becomes a loop with one overflow check per return; ADR 0044) | M | **done** (both stages): `fib(35)` from 45 ms to 32 ms with `--release` (C 23 ms, Rust 27 ms) |
| 5.15 | Generated-code benchmark suite with C/Rust/Go baselines (started at M2 with the first programs) | M | **done**: `make bench-programs` checks outputs and measures all four languages in CI; first results in `bench/HISTORY.md`, with new results recorded per release |
| 5.16 | Generic type aliases (`type Pair<T> = (T, T)`) | S | **done**: both stages expand aliases; scope, arity, cycles, bounds and storage restrictions tested |
| 5.17 | Key types (spec 16.1-16.3): `keyof`, key literals, `\|` `&` `-` on keys, key-bounded parameters (erased, instantiated per key), `T[K]` and `value[key]` | M | **done** (stage 1): `tests/run/type_operators/keys*` and `key_params` pass; E0340-E0343 tested; moving `value[key]` out needs an owned `value` |
| 5.18 | Mapped types (16.4) with `?:` and `if` filters; conditional types with `infer` (16.5) | L | **done** (stage 1): `Partial`, `Pick`, `Omit`, `Record`, `Required` work in `tests/run/type_operators/mapped_types` and `conditional_types`; E0332, E0340, E0344 and E0345 tested; mapped and conditional types over type parameters wait for 5.19 |
| 5.20 | Union types (spec 17.1-17.3, ADR 0022): normalization, member rules, layout as an enum, widening at the coercion points and in `?` | M | **done** (stage 1): `tests/run/unions/*` pass; E0350, E0351 and E0353 (both stages) tested; widening a union into a larger one, E0352 and unions over type parameters wait for 5.21 |
| 5.21 | Narrowing (17.4): type test patterns in `match` and `if const`, exhaustiveness, sub-unions | M | **done** (stage 1): `tests/run/unions/narrowing*` and `widening_sub` pass; E0352 and E0500 on unions tested; widening a union into a larger one at the coercion points and in `?`; a sub-union test on a borrowed value waits |
| 5.21b | Built-in interfaces on unions (17.5): `Eq`, `Hash`, `Clone`, `Debug`, `Display` dispatching on the tag | S | **done** (stage 1): `tests/run/unions/interfaces` and `compile_fail/types/union_interfaces` pass; `{u}` interpolates the member, `{u:?}` shows it without a variant name, no `Ord`; replacing a std error type by a union waits |

**M5** is reached when every chapter of the specification not marked
**[planned]** is implemented and tested → release 0.5. **Reached**: the
M5 check below has no open gap.

### M5 check

Every `ofir` example of chapters 1-13 was type-checked with stage 1 and
every E0901 the compiler reports was compared with the specification.
Chapters 14-17, implemented in phase 6, were checked the same way
(5.22h-5.22l); they lose their **[planned]** marks once those gaps are
closed. These gaps remain:

| # | Gap | Spec | Done when |
|---|-----|:----:|-----------|
| 5.22a | Fixed arrays `[T; N]` have a type but no values: literals and `[v; N]` with an expected array type, indexing, copies, drops, `for`, `&[T; N]` to `&[T]` and `[T]` parameters | 2.7, 2.9, 2.12 | **done** (stage 1, Copy elements): `tests/run/arrays/*`, `panic/bounds_array` and `compile_fail/types/array_*` pass; arrays of other types are E0901, and stage 0 has no array values |
| 5.22b | `T?` as shorthand for `Option<T>` | 2.6 | **done**: both stages resolve `T?`; `tests/run/option/shorthand` covers parameters, results, fields and generics |
| 5.22c | `&mut [T]`: mutable slices | 2.12 | **done** (stage 1): `tests/run/slices/mutable` writes through slices of a list and an array; `borrow/mut_slices` and `compile_fail/types/mut_slices` cover exclusivity and `&`/`const` sources |
| 5.22d | Mutable globals (`let` at module level, accessed in `unsafe`) | 11.5 | **done** (both stages, numeric and `bool` globals): `tests/run/globals/app` reads, writes and borrows globals, also through a path; `compile_fail/types/globals` covers access outside `unsafe`, run-time initializers and other types |
| 5.22e | `str.toCString()` | 11.3 | **done** (both stages): `tests/run/ffi/cstring` runs the chapter's `strlen` example; `panic/cstring_nul` covers a NUL inside the text |
| 5.22f | `@bench` and `ofir bench` with a `Bencher` | 12.3 | **done** (stage 1): `tests/ofir_bench/sample` runs under `ofir bench` (checked by `run_std_tests.py`), and `compile_fail/types/benches` covers E0206 |
| 5.22g | Chapter 12.1 still says the layout and linking attributes report E0901 | 12.1 | **done**: the note lists what each stage implements; only `@tag` reports E0901 |
| 5.22h | The chapter 17 example called `fs.readFile` and interpolated an `IoError` | 17 | **done**: it calls `fs.readToString` and prints `e.message` |
| 5.22i | Unions whose members contain type parameters (`Option<T> \| int`) are E0901 | 17.1 | **done** (stage 1): `tests/run/unions/generic` returns and narrows `Option<T> \| int` in a generic function |
| 5.22j | A mapped type over a mapped type of a type parameter (`Partial<Partial<T>>`) is E0340 | 16.4 | **done** (stage 1): `tests/run/type_operators/mapped_nested` type-checks and runs a generic signature with `Partial<Partial<T>>` |
| 5.22k | A conditional type over a type parameter is not decided when the pattern alone decides it: `Unwrap<Option<T>>` is not `T` | 16.5 | **done** (stage 1): `tests/run/type_operators/cond_generic` returns `x ?? d` from `first<T>(x: move Option<T>, d: move Unwrap<Option<T>>) T` |
| 5.22l | The chapters 14-17 still carry **[planned]** marks (headers, README, appendix A) | 14-17 | **done**: the marks are gone from the headers, the index and appendix A; each chapter says that stage 1 implements it and stage 0 does not |

## Phase 6 — Towards 1.0

| # | Task | Size |
|---|------|:----:|
| 6.1 | Concurrency (spec chapter 14), in the four steps below | L |
| 6.1a | Tasks: `task.run` and `join` on OS threads, standard output a line at a time; **done** (stage 1) | M |
| 6.1b | `Send` and `Sync` marker interfaces, required by `task.run` (14.3); **done** (stage 1): `+ Send` function types, `@threadsafe`, E0360 (ADR 0030) | M |
| 6.1c | Channels: `channel<T>()`, `send`, `recv` (14.4); **done** (stage 1): `std.sync`, `tests/run/sync/channel` | M |
| 6.1d | `std.sync`: `Mutex`, `RwLock`, atomics, `Arc`, `WaitGroup` (14.4); **done** (stage 1): `runtime/src/sync.c`, `tests/run/sync` | M |
| 6.2 | `ofir fmt` implementing the [style guide](style-guide.md); **done** (stage 1): `compiler/fmt`, `tests/fmt` | M |
| 6.3 | Language server (diagnostics, go to definition, completion), in the steps below; **done** | L |
| 6.3a | `ofir lsp`: JSON-RPC on stdio, documents synchronized whole, diagnostics published; **done** (stage 1): `compiler/lsp`, `tests/lsp` | M |
| 6.3b | Go to definition; **done** (stage 1): links recorded by the resolver and the type checker, `tests/lsp/definition` | M |
| 6.3c | Completion; **done** (stage 1): a probe the resolver and the type checker report with the names visible there, `tests/lsp/completion` | M |
| 6.4 | `ofir doc` and doc tests, in the steps below; **done** | M |
| 6.4a | Doc tests: the ```` ```ofir ```` blocks of documentation comments run under `ofir test`; **done** (stage 1): `compiler/driver/doctest.of`, `tests/ofir_doctest`, the examples of std and `compiler/` | S |
| 6.4b | `ofir doc`: HTML documentation from the doc comments; **done** (stage 1): `compiler/doc`, `tests/doc` | M |
| 6.5 | Package manager (ADR 0032, stage 1), in the steps below; **done** | L |
| 6.5a | Design: ADR 0032, `docs/design/toolchain.md`, codes E0910-E0915; **done** | S |
| 6.5b | Path dependencies: a JSONC reader for `ofir.json`, `dependencies` with `path`, imports by package name (E0910, E0911, E0915); **done** (stage 1): `tests/run/packages/app` and `tests/compile_fail/packages` | M |
| 6.5c | Git dependencies, semver requirements, resolution, `ofir.lock`, `ofir fetch` and `ofir update` (E0912); **done** (stage 1): `tests/run/packages/git` and `tests/compile_fail/packages/git` | M |
| 6.5d | SHA-256 checksums in `ofir.lock`, `--locked` and `--offline` (E0913, E0914); **done** (stage 1): `tests/compile_fail/packages/{locked,checksum,offline}` | S |
| 6.5e | Registry client: the git index and version archives; **done** (stage 1): `tests/run/packages/{registry,yanked}` and `tests/compile_fail/packages/{registry,registry_checksum}` | M |
| 6.6 | Incremental and parallel compilation (ADR 0033, stage 1), in the steps below; **done** | L |
| 6.6a | Design: ADR 0033, measurements of a self-compile; **done** | S |
| 6.6b | `os.spawn`, `Child.wait` and `os.cpuCount` in std (runtime, both stages); **done**: `tests/run/system/os` | S |
| 6.6c | Codegen units: one per module, stable text within a unit, `hidden` and `linkonce_odr` linkage, `--codegen-units=1`; `--emit=llvm` unchanged; **done**: stage 1 builds a `.ll` file per module and links them in one clang call; all tests pass with units, and `verify` checks the single module | M |
| 6.6d | Parallel clang with `-j N` and a separate link step; **done**: the units are compiled by at most `-j N` clang processes (`--jobs N`, by default the cores), then linked; a self-compile at `-O0` takes 19.5 s with 4 jobs, against 24.8 s as one module, after the layouts of ADTs are cached | S |
| 6.6e | The object cache in `$OFIR_HOME/cache/objects`: keys, atomic writes, checks, recovery after a failed link, `OFIR_CACHE_LIMIT`, `--no-cache`; cold and warm build times recorded; **done**: a self-compile at `-O0` on 4 cores takes 22.7 s cold and 10.6 s warm, when every unit hits the cache | M |
| 6.6f | Release benchmarks with units and with one unit; the release default chosen from them; **done**: release builds are one unit. Run times with units against one unit: `fib` -9%, `nbody` -2%, `binary_trees` +2%, and the compiler compiling itself to `.ll` +59% (1.38 s against 0.87 s), past the 3% of ADR 0033 | S |
| 6.7 | Seeded `Map` hasher, labeled loops, const generics; labeled loops **done** (`name: for`, `break name`, `continue name`; spec 4.4); seeded hasher **done** (`Map.seeded()`, `Map.withSeed`, SipHash-1-3 strings; ADR 0034); const generics **done** in stage 1 (`const N: int`, `[T; N]`, inferred from array lengths; spec 8.7) | M |
| 6.7a | Compile-time code: `comptime`, `std.meta`, `comptime for`/`if`, `@tag` (spec chapter 15, ADR 0035, stage 1), in the steps below; **done** | L |
| 6.7a1 | Design: ADR 0035 (checked once, expanded per instance), spec chapter 15, codes E0363, E0364, E0814-E0818; **done** | S |
| 6.7a2 | `comptime` keyword, `std.meta` predicates and `nameOf`, `comptime if` with narrowing (bounds and numeric operators), `comptime panic` (E0363, E0814); **done** | M |
| 6.7a3 | `comptime for` over `keyof T` and `meta.fieldsOf<T>()`, `@tag` and `field.attribute`; done when `toJson` (spec 15.3) runs (E0364); **done** | M |
| 6.7a4 | `meta.variantsOf<T>()`: `variant.name`, `index`, `attribute(key)` and `matches(value)`, `@tag` on variants; **done** | S |
| 6.7a5 | AIR interpreter: `comptime` expressions, `comptime fn`, the step budget and `@comptimeBudget` (E0815-E0818); **done** | M |
| 6.7a6 | Module-level `const X = comptime f()`, of any `comptime` value (E0801 where its value is needed during checking); **done** | S |
| 6.7a7 | Compile-time heap allocation (`List`, `Map`) inside `comptime` code; **done** | S |
| 6.7a8 | Faster `comptime` interpreter: the checker and program are passed by reference, not copied on each call (about 6x faster); **done** | S |
| 6.7b | `comptime for` over keys (spec 16.6) and the standard aliases in `std.core` (16.7), moved from 5.19: it needs 6.7a, mapped and conditional types over type parameters, and type operators that stage 0 can load in `std.core`; done when `merge(base, patch)` is written in Ofir | M |
| 6.7b1 | Mapped types over type parameters: `Partial<T>` in a generic signature, `patch[key]` in `comptime for key in keyof T`, inference from an instance; `merge(base, patch)` (spec 16.6) runs with a local alias; **done** | M |
| 6.7b2 | The standard aliases in `std.core` (spec 16.7): stage 0 loads type operator aliases as opaque (E0901 on use); in an alias body, the using module's private fields are keys too; **done** | S |
| 6.7b3 | Filters, `Pick`/`Omit` and conditional types over type parameters (`Pick<T, K>`, `Unwrap<T>` in a generic signature); **done** | M |
| 6.8 | Windows support (x86_64, clang with the MinGW target), in the steps below; **done** | L |
| 6.8a | CI job on Windows: both stages build, all tests and std tests pass with stage 0 and stage 1 (paths with `\` and drive letters in `fs`, `ofir lsp` and `ofir doc`; `// requires: unix`); **done** | M |
| 6.8b | Windows tests of target-specific code: `@cfg(os == "windows")` output, `os.name()`, `// requires: windows`; **done**: `tests/run/attributes/cfg_windows` | S |
| 6.8c | Releases checked on Windows: `release.yml` builds the compiler from the seed there (`make check-seed`), and a `publish` input checks a branch without releasing; **done** | S |
| 6.8d | Installing and using Ofir on Windows in the docs (`docs/design/toolchain.md`, README); **done** | S |
| 6.9 | Scheduled CI job for the auditable bootstrap path: `tools/bootstrap_chain.sh`, weekly; the link from the last release to the current sources waits for the next release; **done** | S |
| 6.10 | Website, tutorial ("The Ofir Book"), playground | L |
| 6.32 | Asserts and `Bencher` in `std.testing`, imported instead of in the prelude (ADR 0036), in the steps below; **done** | M |
| 6.32a | Design: ADR 0036, spec 7.5, 12.2 and 12.3; **done** | S |
| 6.32b | `std.testing` with the `assert` intrinsic, recognized by both stages; `assert` leaves the prelude, and E0200 on `assert` names the import; **done**: `tests/compile_fail/names/assert_import` | M |
| 6.32c | Migration: the tests, the std and compiler modules and the doc tests import `std.testing.assert`; **done** | M |
| 6.32d | `assertEq` and `assertNe` (stage 1), with `Debug` output on failure; **done**: `tests/run/testing/assert_eq`, `tests/panic/assert_eq` | S |
| 6.32e | `Bencher` moves to `std.testing`; `ofir bench` no longer adds it to the prelude; **done**: `tests/ofir_bench/sample` imports it | S |

### API and service ecosystem (proposed)

The [API ecosystem plan](design/api-ecosystem.md) inventories the current
implementation and defines **49 pending deliverables**, API-01 through API-49,
with priorities, dependencies, library/package placement and completion tests.
The following epics extend phase 6; they are proposed work, not implemented
features or additional commitments for language version 1.0.

| # | Epic | Deliverables | Completion gate |
|---|------|--------------|-----------------|
| 6.11 | Stream I/O, filesystem/paths, time/date, data types, errors, config and lifecycle | API-01 to API-08 | Portable contracts, limits and resource cleanup tested |
| 6.12 | Entropy/crypto providers, sockets/DNS, TLS and bounded concurrency | API-09 to API-12 | Interoperability, deadlines, overload and cancellation tested |
| 6.13 | JSON, typed serialization, URLs/forms/multipart and compression | API-13 to API-16 | Malformed input, precision, streaming and size limits tested |
| 6.14 | HTTP client/server, HTTP/1.1 and optional HTTP/2/HTTP/3 | API-17 to API-21 | Protocol corpus and client/server interoperability pass |
| 6.15 | REST router/middleware, validation, API conventions, OpenAPI, cookies/sessions/CORS | API-22 to API-26 | Reference routes, schema compatibility and error contracts tested |
| 6.16 | Authentication/authorization and service resilience | API-27 to API-28 | Credential, tenant, retry and quota failure cases tested |
| 6.17 | Database contracts/drivers, pools/transactions, migrations, ORM and cache | API-29 to API-37 | Real-database tests, rollback, cancellation and precision pass |
| 6.18 | Object storage, jobs/queues, realtime and optional protocol/provider adapters | API-38 to API-41 | Delivery/consistency contracts and disconnect/retry behavior tested |
| 6.19 | Structured logs, metrics and distributed traces | API-42 to API-44 | Correlation, redaction, cardinality and bounded exporters tested |
| 6.20 | API test tools, protocol conformance, fault/load tests and portability | API-45 to API-46 | Reproducible latency/resource results and supported-OS matrix |
| 6.21 | Package delivery, deployment lifecycle and reference CRUD service | API-47 to API-49 | Clean-machine build plus local and production-example gates |

Recommended progression: **API-F** foundations, **API-L** local REST + SQLite,
**API-P** production candidate + PostgreSQL/TLS/auth/observability, then **API-X**
optional integrations. The linked plan defines the exact gate membership.
Use blocking I/O with bounded workers first, following chapter 14; optional
async and automatic codec/schema generation must not block the first service.

### Language-wide completeness (proposed)

The [master capability inventory](design/language-completeness.md) expands the
survey beyond APIs: **86 capability records**, OFIR-01 through OFIR-86, covering
the language, runtime, compiler, general libraries, tools and ecosystem. Records
audit or extend existing tasks as well as expose new work; they are not 86 absent
features or independent implementation tickets. Reuse phase 5/6 tasks and the
API subplan wherever they overlap. Existing completion markers remain unchanged.

G0 establishes implementation truth and safety; G1 proposes a general-purpose
baseline; G2 identifies optional specializations. Select the release subset
explicitly rather than treating this inventory as a new 1.0 commitment.

| # | Coverage epic | Capability records | Completion gate |
|---|---------------|--------------------|-----------------|
| 6.22 | Specification, conformance inventory and compatibility policy | OFIR-01 to OFIR-07 | Guarantees mapped to source/tests, stages, targets and feature states |
| 6.22a | Conformance matrix: every spec section mapped to stages, tests and classified gaps (`docs/design/conformance.md`, checked by `tools/check_conformance.py`) | OFIR-01 | **done**: `make check-docs` checks the matrix against the spec |
| 6.22b | Boundary cases: evaluation order (spec 4.10), literal and cast edges (1.7, 2.1), newline rules (1.3); float-to-int `as` checks the truncated value | OFIR-02 | **done**: cited in the conformance matrix |
| 6.22c | Unsafe obligations: validity, initialization, alignment, aliasing, provenance and data races (spec 11.6); `&x as *T` is safe | OFIR-03 | **done**: provenance and weaker atomic orderings are [open] |
| 6.22d | Compatibility policy: versions, breaks, deprecation, editions, upgrades (`docs/design/compatibility.md`, ADR 0037) | OFIR-04 | **done**: migration fixtures; editions are [planned] |
| 6.22e | Equality, ordering and hashing contracts (spec 9.9), NaN and signed zero (2.1), custom and seeded map keys; a declared `eq` no longer gets a derived `Ord` | OFIR-05 | **done**: `Hasher`, `totalCmp` and `cmp` on primitives stay open |
| 6.22f | Source encoding (UTF-8, E0008), bidirectional controls (E0009), ASCII tokens and identifiers, byte columns in diagnostics (spec 1.1) | OFIR-06 | **done**: Unicode identifiers are not planned |
| 6.22g | Feature states: planned, open and rejected features with owners and gates (`docs/design/feature-states.md`, ADR 0038), ADR statuses checked by `tools/check_features.py`; no feature flags before 1.0 | OFIR-07 | **done**: 6.22 is complete |
| 6.23 | Language completion and unresolved semantic choices | OFIR-08 to OFIR-21 | Existing phase 5 tasks linked; accepted semantics and rejection cases tested |
| 6.23a | Interfaces: embedding cycles (E0327), conflicting embedded signatures (E0328) and conflicting defaults through embedding, assertions and bounds (E0329) in spec 9.3 and 9.4; merged signatures, diamonds and recursive types tested | OFIR-08 | **done**: embedding a generic interface stays a stage 1 limit |
| 6.23b | Dynamic dispatch: `mut dyn` parameters, `dyn` values and boxes passed to `dyn` parameters, `Box<dyn I>` conversions in generic arguments, the vtable layout (spec 9.7); type tests on generic types and ownership through `Box<dyn Any>` tested | OFIR-09 | **done**: upcasts and `dyn` of generic interfaces are planned |
| 6.23c | Closures and function values: `x.f()` calls a fn-typed field (spec 10.2); escape of borrowed `fn` values (E0702), conversion to `move fn`, drops of `move fn` in fields, lists and options tested; `move fn` printed in types | OFIR-10 | **done**: stage 0 keeps no field calls and no E0702 |
| 6.23d | Generic std and iteration: `for mut` binds elements in place (both stages), borrow contracts of spec 4.4 tested (E0700, E0701, E0703, E0315), `List.drain()` for consuming loops, no lazy adapter chains (ADR 0040) | OFIR-11 | **done**: stage 0 checks no loop borrows; references stored in a container are not yet tracked by the borrow checker |
| 6.23e | Named/default arguments and string forms: evaluation order of written arguments and defaults, multi-byte format widths and UTF-8 boundaries tested; `str.chars()` decodes scalar values (both stages) | OFIR-12 | **done**: named and default arguments, raw and multi-line strings and format specifiers stay stage 1 only |
| 6.23f | Attributes and conditional compilation: `@cfg` checks only its condition and follows the host target, `main` and `ofir_` C symbols are reserved for the runtime (E0819), deprecation and unknown attributes tested | OFIR-13 | **done**: stage 1 only; no cross compilation |
| 6.23g | Compile-time execution: E0815 and E0816 note the function and line where a called function failed and the calls that led there; a random seed is rejected (E0815); the test runner checks diagnostic notes | OFIR-14 | **done**: stage 1 only |
| 6.23h | Type operators: a type alias may name itself in a branch of a conditional type, decided per expansion, up to 64 nested expansions (E0346); visibility (`key_visibility`), inference (`mapped_generic`) and ownership (`key_params`, E0600) of keys stay tested | OFIR-15 | **done**: stage 1 only; recursion on a type parameter is E0901 |
| 6.23i | Union types: normalization (`unions/normalization`), layout (`unions/layout`), exhaustiveness (`union_unreachable`, W0004) and invalid narrowing (`union_narrowing`: E0352 for sub-unions and `if const`, E0500, E0602 from a borrowed member) are tested | OFIR-16 | **done**: stage 1 only; the spec needed no change |
| 6.23j | Array-length expressions: `+ - * / %` on integer constants, `const` items and const parameters (`[float; R * C]`, `[0; N * 2]`); exact evaluation from 0 to 4294967295, checked where written or at each instance (E0366); other length expressions are E0362 (spec 8.7) | OFIR-17 | **done**: stage 1 only; lengths are equal only as written, so `N + 1` and `1 + N` differ |
| 6.23k | Patterns and bindings: `name @ pattern` with alternatives (`x @ 10 \| 20`), in arms, `if const`, `const`, variants and fields; the inner pattern binds nothing (E0502); string patterns and guards confirmed; slice patterns deferred to OFIR-19 (ADR 0043) | OFIR-18 | **done**: stage 1 only |
| 6.23l | Borrow usability: reborrows (`&mut *r`, writes through `r`) borrow the reference; `&xs[a..b]` and `&mut xs[a..b]` (`sliceMut`, `sliceFromMut`) are views that borrow `xs`; disjoint fields and reference returns confirmed; `&mut str` is not a type; slice patterns stay deferred (ADR 0045) | OFIR-19 | **done**: stage 1 only; views of one list conflict whatever their ranges |
| 6.23m | Modules: `pub import` re-exports modules and public declarations (chains, aliases; private is E0202); nested paths `a.b.Name` in expressions and types; a submodule is reachable only by import or re-export; initialization is static and import cycles are safe (ADR 0046) | OFIR-20 | **done**: stage 1 only |
| 6.23n | Numeric and type gaps: arrays get the methods of `&[T]`, `a[lo..hi]`, `==` and `{a:?}`; `==` on `&[T]` compares elements (`sliceEqual`); range values and `i128`/`u128` deferred with a help (ADR 0047) | OFIR-21 | **done**: stage 1 only |
| 6.24 | Runtime, ownership, allocation, concurrency and ABI | OFIR-22 to OFIR-30 | Resource, failure, layout and concurrency contracts verified |
| 6.24a | Atomic intrinsics with orderings, `@threadLocal` and pointer, array and `@repr(C)` globals in both stages, then the allocator pool in Ofir (ADR 0042) | OFIR-25 | in progress: `@threadLocal`, the atomic intrinsics (`std.mem.atomic`, E0820, E0821), pointer globals (`0 as *T`), E0822 and `AtomicInt`/`AtomicBool`/`Arc` on the intrinsics (`...With` forms; `ofir_atomic_*` removed) and the pool in Ofir (`std/core/pool.of`, `@export` in stage 0) done; array and `@repr(C)` globals later |
| 6.24b | Ownership and destruction across partial moves, control flow and errors: adversarial drop tests (`tests/run/drops`), and the valgrind and ASan checks run with stage 1, which compiles every run test; a panic's leaks are by design (spec 6) | OFIR-22 | **done** |
| 6.24c | Allocation failure policy (ADR 0048, spec 6.5): `capacity overflow` for a size that does not fit an `int` or is negative, `out of memory` for a failed allocation, both tested in `tests/panic`; zero-sized values (`List<()>`, enums with zero-sized payloads) fixed in codegen of both stages; fallible allocation deferred | OFIR-23 | **done** |
| 6.24d | Shared ownership (spec 5.9): `Rc`/`Weak` in `std.mem.rc` (stage 1), counts, `upgrade` after the last owner, a tree with `Weak` parent links freed by the root, and `Rc`/`Weak` rejected as `Send`; cycles of strong owners leak and are documented | OFIR-24 | **done** |
| 6.24e | Concurrency contracts (spec 14): `boundedChannel` whose sends wait while full, task handles dropped without a join detach the task and drop its result, and stress tests for bounded queues, release/acquire handoff, a spin lock, nested and many joins; relocking a held mutex waits forever (spec 14.4) | OFIR-25 | **done** |
| 6.24f | Cancellation, deadlines and shutdown (spec 14.5): `std.time` (monotonic `now`, `sleep`, `Deadline`), `Receiver.recvUntil` (`Recv<T>`), `WaitGroup.waitUntil`, cooperative `CancelToken`; a worker pool drained, then cancelled with queued work, drops every job and ends before its deadline | OFIR-26 | **done** |
| 6.24g | Ending the process (spec 6.6): no unwinding on panic, `os.exit` or the new `os.abort` (`SIGABRT`, no flush); the first thread to end the process owns the end, so tasks panicking at once print one message; `OFIR_BACKTRACE` prints the panicking thread's return addresses; an `@export` callback that panics never returns to C; run tests can expect an exit status | OFIR-27 | **done** |
| 6.24h | Allocators and arenas (ADR 0049): measured on binary-trees (`Box` 1.76 s, `std.arena` 0.49 s, bare `List` 0.33 s); `std.arena` adds `Arena<T>` with `copy` `Id<T>` handles checked by era, `clear` keeps the storage; allocator parameters on collections and a global allocator hook rejected until a workload needs them | OFIR-28 | **done** |
| 6.24i | Async, green tasks and interop (ADR 0050): OS-thread tasks measured (35.5 µs per spawn and join, about 9 KB per blocked task, 30,000 alive in 270 MB); no `async`/`await`, M:N scheduler deferred until a workload needs it; cooperative cancellation; no references across suspension (`move` + `Send` closures); foreign calls hold their thread | OFIR-29 | **done** |
| 6.24j | Layout and ABI contracts (ADR 0051): C fixtures compiled by the test runner (`// c:`) check every scalar type both ways, 12 mixed arguments, exports called from C, and `@repr(C)`, nested, `@packed` and `@align` structs and `@repr(C)` enums behind pointers; `tests/run/layout/sizes.of` records sizes and alignments; spec 11.3 and 13 now state the implemented layouts; by-value `@repr(C)` types, `extern "C" fn` types and a compact enum layout are planned | OFIR-30 | **done** |
| 6.25 | Compiler correctness, bootstrap, build performance and debug information | OFIR-31 to OFIR-38 | Invariants, parity, cache invalidation and debugger evidence recorded |
| 6.25a | Bounded frontend (ADR 0052): both parsers count nesting (parentheses, blocks, operators, unary and `as` chains, postfix steps, `else if`, types and patterns) and stop at 256 levels with one E0101; deep and adversarial inputs end in that diagnostic instead of a stack overflow; `compare_stages.py` builds stage 1 in release mode, as the Makefile does | OFIR-31 | **done** |
| 6.25b | AIR invariants: stage 1 gains the structural AIR validator of stage 0 (`air/validate.of`); both stages validate every body after lowering and again after check folding, and a malformed body prints the body, a note and `invalid AIR after <pass>` naming the function, block and fault before any LLVM IR; unit tests in both stages cover each fault | OFIR-32 | **done** |
| 6.25c | Auditable bootstrap: `make check-seed` compiles only a seed whose SHA-256 is recorded (`tools/check_seed_sum.py`: its `.sha256`, else `seed/SEEDS.md`) and the compiler built from it rebuilds itself to a fixed point; seed and chain compilers are release builds, so they pass the deep-nesting tests; `docs/bootstrap.md` lists stage 2/3 parity, shared-subset parity and the independent `requires: stage1` tests | OFIR-33 | **done** |
| 6.25d | Incremental builds: `tools/check_object_cache.py` edits a copy of a program and checks that the object cache key, the unit's IR text plus clang flags, path and version, follows dependencies (a comment compiles no unit, a body its own unit, a layout its module and its users) and recovers from corrupt objects and failed links; cold, warm and one-edit self-compile times are recorded in `docs/design/performance.md` §6 | OFIR-34 | **done** |
| 6.25e | Optimization tracking: `bench/run_programs.py` reports executable sizes and, for Ofir, the release compile time, the build's peak memory and the number of functions in the optimized IR (monomorphization); `tests/panic/release_*.of` check that `--release` keeps overflow, bounds, division and `as` range checks; results in `bench/HISTORY.md` | OFIR-35 | **done** |
| 6.25f | Debugger support: `-g` describes named parameters and locals (`DILocalVariable`, `llvm.dbg.declare`) and their types (basic types, pointers, structs with their fields, `str`), the prologue has no line so a function breakpoint stops at its first statement, `--release -g` marks the unit optimized; `tools/check_debugger.py` runs gdb on a debug and a release build | OFIR-36 | **done** |
| 6.25g | Compiler service interface (ADR 0053): `ofir check` (diagnostics, no output), `ofir metadata` (the modules of a program as versioned JSON), a documented exit status (0, 1, 2, 101, the program's own under `run`), cancellation by signal with an object cache only renamed into, and the language server's in-order, never-cancelled requests; `tools/check_service.py` checks them | OFIR-37 | **done** |
| 6.25h | Fast debug compilation (ADR 0054): a profile of the self-build (front end 1.2 s, clang `-O0` 15.7 s of 17.3 s cold, 1.5-3 s edit-rebuild with the object cache, FastISel falling back on aggregates by value and `fastcc`) rules out an alternative backend or JIT; debug code generation uses the linear pre-RA scheduler (17.3 s to 11.6 s cold); aggregates by pointer in debug IR is the recorded next lever | OFIR-38 | **done** |
| 6.26 | General-purpose standard library and public API quality | OFIR-39 to OFIR-47 | Documented exports, generic usage, portability and boundary tests |
| 6.26a | Built-in interfaces audit: generic user structs and enums work through `Eq`, `Ord`, `Hash` (derived), `Clone` and `Debug`, inside `Option`, `Result`, `List`, `Map` and tuples (`tests/run/interfaces/builtin_audit.of`); stage 1 gains `Display` (`fn format(self, out: mut str)` for `{x}` and the `T: Display` bound) and the `&List<T>` to `&[T]` conversion of spec 2.12, also for an inferred `T`; open: a user-declared `hash` waits for `Hasher`, and `T.default()` on a type parameter for static interface methods | OFIR-39 | **done** |
| 6.26b | Collections by workload in `std.collections` (ADR 0055): `Deque` (ring buffer), `Set` (over `Map`), `Heap` and `Reverse` (binary heap), `SortedMap` (sorted parallel lists, `lowerBound` ranges); `Map.keys` and `List.swap`; ordering, capacity reuse and drop-exactly-once tests; a B-tree waits for a benchmark | OFIR-40 | **done** |
| 6.26c | Text by level (ADR 0056): `str` gains `rfind`, `splitOnce`, `stripPrefix`/`stripSuffix`, `asBytes`, `isCharBoundary`, `charCount`, `charIndices`, `isAscii` and `eqIgnoreAsciiCase`; `std.strings.utf8` (`isScalar`, `charLen`, `encode`, `decode`, `lossy`) and `std.strings.ascii` (classes, case, `digitValue`); `Builder.withCapacity`; graphemes, normalization, regex and locale stay separate decisions | OFIR-41 | **done** |
| 6.27 | FFI, native libraries, target tiers and cross compilation | OFIR-48 to OFIR-55 | Real foreign consumers and supported-target execution pass |
| 6.28 | Formatting, LSP, diagnostics, debugging, profiling and documentation | OFIR-56 to OFIR-63 | End-to-end developer workflows work on supported platforms |
| 6.29 | Packages, build inputs, registry, installation and releases | OFIR-64 to OFIR-71 | Clean-machine/offline builds, integrity and compatibility evidence |
| 6.30 | Conformance, fuzzing, safety, performance, CI and build security | OFIR-72 to OFIR-78 | Reproducible checks plus representative applications across releases |
| 6.31 | Service ecosystem, learning, domain packages and maintenance | OFIR-79 to OFIR-86 | Scoped deliverables, maintained examples, owners and explicit optional decisions |

Keep stage 0 minimal. GUI, scientific computing, mobile, embedded, WebAssembly,
alternative backends and async require separate workload and maintenance
decisions. The master plan defines acceptance evidence and architectural
boundaries; coverage epics must be split into small implementation PRs.

## Releases

| Version | Content |
|---------|---------|
| 0.1 | M2: stage 0 compiler usable for small programs |
| 0.2 | M4: self-hosted compiler with borrow checker; first seed |
| 0.5 | M5: full language |
| 1.0 | concurrency, formatter, language server, package manager; stability guarantee for the language and `std` |

## Open questions

These do not block Phases 0–4 and will be settled with an ADR when needed:

1. Lazy iterator chains (`list.iter().map(f).filter(g)`) versus eager
   `List.map`/`filter` only (before task 5.8).
2. ~~More patterns: strings, `name @ pattern` bindings.~~ Settled (ADR 0043);
   slice patterns (`[first, ..rest]`) wait for a use case (ADR 0045).
3. ~~Labeled `break`/`continue` (task 6.7).~~ Settled: `name: for`, as in Go and Zig.
4. Error handling conventions: error context and cause chains in `std`.
5. 128-bit integers.
6. The M:N scheduler for lightweight tasks (ADR 0027): task stacks, non-blocking I/O, blocking C calls.
7. Packages: registry hosting, versioning, `pub import` re-exports.
8. Custom allocators and arenas in `std`.
9. Advanced FFI: callbacks from C, variadic C functions, `union`.
10. ~~Stability policy~~ Settled before 1.0 (ADR 0037); the 1.0 promise and editions stay open.
11. Final project name and repository rename (risk R9).

## Next steps

1. Continue 6.26 with OFIR-42; OFIR-39 to OFIR-41 are 6.26a to 6.26c.
2. Advance general-purpose std, tooling, packages and installation alongside
   the API foundation contracts in 6.11 and the JSON codec in 6.13,
   using the dependency and priority order in the
   [API ecosystem plan](design/api-ecosystem.md). Coordinate interfaces (5.3)
   and concurrency (6.1) before committing to shared stream/driver contracts
   or a concurrent server.
3. Validate a CLI/filesystem utility, C consumer and concurrent program
   (OFIR-76), plus each API milestone with the reference service (API-49).
   Select optional adapters and domain ecosystems by concrete demand and ownership.

See also the [risk register](risks.md).
