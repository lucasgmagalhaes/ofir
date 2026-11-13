# Project structure

This document describes the layout of the Ofir repository and the
responsibility of each directory. It is the reference for where new code
belongs.

## Overview

```
ofir/
├── README.md
├── LICENSE
├── CONTRIBUTING.md
├── CHANGELOG.md
├── Makefile                  top-level build: stage0, stage1, stage2, test, verify
├── .editorconfig
├── .github/
│   └── workflows/
│       ├── ci.yml            build all stages, run all test suites
│       └── release.yml       build release binaries
│
├── docs/
│   ├── spec/                 language specification (normative)
│   ├── std/                  standard library reference (normative)
│   ├── errors/               error code registry and explanations
│   ├── design/               architecture and design documents
│   ├── adr/                  architecture decision records
│   ├── bootstrap.md          self-hosting plan and bootstrap chain
│   ├── roadmap.md            implementation roadmap
│   └── risks.md              risk register
│
├── bootstrap/                stage 0 compiler, written in C
├── compiler/                 stage 1 compiler, written in Ofir
├── runtime/                  C runtime linked into every Ofir program
├── std/                      standard library, written in Ofir
├── tests/                    language and compiler test suites
├── examples/                 example programs
├── bench/                    performance benchmarks
├── seed/                     SEEDS.md: checksums of released compiler seeds
└── tools/                    developer scripts and auxiliary tools
```

## `bootstrap/` — stage 0 compiler (C)

```
bootstrap/
├── Makefile
├── README.md
├── src/
│   ├── main.c                command line parsing
│   ├── driver.c/.h           pipeline: runs the phases on a session, invokes clang
│   ├── session.c/.h          state shared by all phases (arena, sources, names, diagnostics)
│   ├── base/                 foundation, no compiler knowledge
│   │   ├── arena.c/.h        arena allocator (all compiler memory)
│   │   ├── vec.h             generic dynamic array macros
│   │   ├── map.c/.h          hash map (string interning, symbol tables)
│   │   ├── str.c/.h          string builder and string views
│   │   ├── intern.c/.h       interned identifiers
│   │   └── source.c/.h       file table, spans, line and column lookup
│   ├── diag/
│   │   └── diag.c/.h         diagnostics: collection and rendering
│   ├── syntax/
│   │   ├── token.c/.h        token kinds, names and printing
│   │   ├── lexer.c/.h        source → tokens: main loop, comments, words, punctuation
│   │   ├── lexer_internal.h  state shared by the lexer files
│   │   ├── literals.c        numbers, strings and byte literals
│   │   ├── ast.h             AST node definitions
│   │   ├── ast_print*.c      AST dump for `--emit=ast` and tests
│   │   ├── ast_text.c/.h     one-line text for types, patterns and literals
│   │   ├── parser.c/.h       tokens → AST: entry point, token helpers, recovery
│   │   ├── parser_internal.h state shared by the parser files
│   │   ├── parse_items.c     items, imports, extern blocks, attributes
│   │   ├── parse_decls.c     functions, structs and enums
│   │   ├── parse_stmts.c     statements
│   │   ├── parse_control.c   blocks, `if`, `match`, `unsafe`
│   │   ├── parse_exprs.c     operator precedence
│   │   ├── parse_primary.c   primary and postfix expressions
│   │   ├── parse_types.c     types
│   │   └── parse_patterns.c  patterns
│   ├── sema/
│   │   ├── ids.h             TypeId, DefId, LocalId
│   │   ├── types.c/.h        type representation and interning
│   │   ├── sema.c/.h         definitions, modules, `@lang` lookups, phase entry points
│   │   ├── scope.c/.h        one table per module and the local binding stack
│   │   ├── resolve.c         top-level declarations of every module, `@lang`
│   │   ├── resolve_import.c  imports, the prelude, qualified names
│   │   ├── resolve_body.c    names in function bodies
│   │   ├── resolve_internal.h state shared by the resolver files
│   │   ├── check.c           type checking: modules, items, extern blocks
│   │   ├── check_types.c     type expressions and signatures
│   │   ├── check_expr.c      expressions
│   │   ├── check_stmt.c      statements, blocks and `if`
│   │   ├── check_pattern.c   patterns
│   │   ├── check_match.c     `match` arms and their types
│   │   ├── exhaustive.c      exhaustiveness and unreachable arms (usefulness)
│   │   ├── check_method.c    method calls on primitives (std methods, then built-ins)
│   │   ├── check_user_method.c method calls on user types
│   │   ├── check_box.c       dereferencing boxes and references: `*b`, `b.x`, `b.m()`
│   │   ├── check_clone.c     which types can be cloned, the derived `.clone()` (E0322)
│   │   ├── check_iter.c      `for x in it` over a type with `next(mut self) Option<T>`
│   │   ├── check_ref.c       `&x`, `&mut x`, second-class rules (E0702), returned borrows
│   │   ├── check_list.c      `List<T>`: literals, indexing, `for`, `sort` in stage 0
│   │   ├── check_option.c    `?`, `??`, unused results, the Option/Result layouts
│   │   ├── check_generic.c   type arguments: explicit and inferred
│   │   ├── check_unsafe.c    `unsafe` contexts and raw pointers
│   │   ├── structs.c         struct fields, literals, `@lang` layouts
│   │   ├── enums.c           enum variants and payloads
│   │   ├── const_eval.c/.h   constant evaluation
│   │   ├── builtins.c/.h     the methods still built in until bounds (ADR 0019)
│   │   └── bounds.c          generic bounds and `where` clauses (planned)
│   ├── air/
│   │   ├── air.c/.h          Ofir Intermediate Representation
│   │   ├── air_print.c/.h    AIR dump (`--emit=air`)
│   │   ├── air_validate.c    structural checks after lowering
│   │   ├── lower.c/.h        typed AST → AIR: functions reachable from `main`
│   │   ├── lower_internal.h  state shared by the lowering files
│   │   ├── lower_stmt.c      statements and loops
│   │   ├── lower_expr.c      expressions, calls, `if`, `&&`/`||`
│   │   ├── lower_ops.c       checked operators, casts, interpolation, printing
│   │   ├── lower_method.c    method calls, numeric constants, indexing, panics
│   │   ├── lower_intrinsic.c `extern "intrinsic"` calls → AIR operations
│   │   ├── lower_pattern.c   patterns and `match`
│   │   ├── lower_struct.c    struct and enum values
│   │   ├── lower_list.c      list literals, bounds-checked elements, `for` over lists and maps
│   │   ├── lower_hash.c      `hashOf<K>` and `equals<K>` for Map keys
│   │   ├── lower_clone.c     clone glue, the derived `.clone()` and `cloneOf<T>`
│   │   ├── lower_drop.c      drop glue, `dropInPlace<T>` and `forget<T>`
│   │   ├── lower_iter.c      `for x in it`: `next()` until `None`
│   │   ├── lower_test.c      `ofir test`: the tests as roots, the generated entry point
│   │   ├── lower_scope.c     drop scopes, drop flags (also per moved part), temporaries
│   │   ├── lower_option.c    `?` and `??`
│   │   ├── mono.c            monomorphization: instances, type substitution
│   │   ├── moves.c           move analysis: states, statements, loops (E0600-E0602)
│   │   └── moves_expr.c      move analysis of expressions, places and patterns
│   ├── codegen/
│   │   ├── llvm.c/.h         AIR → textual LLVM IR: module, names, constants
│   │   ├── llvm_internal.h   state shared by the generator files
│   │   ├── llvm_types.c      struct, enum and tuple types and instances, field paths
│   │   ├── llvm_body.c       functions, statements, terminators
│   │   ├── llvm_ops.c        operands and rvalues
│   │   ├── llvm_misc.c       string, float and pointer operations, aggregates
│   │   ├── llvm_cast.c       range checks of `as`
│   │   ├── out.c/.h          buffered text writer
│   │   └── abi.c/.h          type layout and calling convention (planned)
│   ├── driver.c/.h           the front end phases and the command line options
│   ├── driver_modules.c      loading the root file, its imports and the prelude
│   ├── driver_paths.c        finding the installed runtime and std
│   ├── driver_backend.c      lowering, code generation, clang, `run`
│   ├── driver_test.c         `ofir test`: one process per test, the report
│   └── driver_internal.h     state shared by the driver files
└── tests/                    unit tests for the C code (base/, lexer)
```

Stage 0 implements only the bootstrap subset (see `docs/bootstrap.md`). It
mirrors the structure of stage 1 so that porting is mechanical.

## `compiler/` — stage 1 compiler (Ofir)

```
compiler/
├── main.of                   command line entry point
├── driver/
│   ├── modules.of            loading the root file, its imports and the prelude
│   ├── cli.of                argument parsing, subcommands
│   ├── session.of            compilation options, target, paths
│   ├── pipeline.of           runs the phases in order
│   └── toolchain.of          locating and invoking clang / linker
├── base/
│   ├── intern.of             interned names (`Symbol`, `Interner`)
│   ├── span.of               source positions, file table (`Span`, `SourceMap`)
│   └── index_vec.of          typed indices (`Id<T>`, `IndexVec<T>`)
├── diag/
│   └── index.of              `diag`: diagnostics and their rendering (stage 0's format)
├── syntax/
│   ├── token.of              token kinds and values
│   ├── cursor.of             position in a source file
│   ├── literals.of           numbers, strings and byte literals
│   ├── lexer.of              main loop, comments, words, punctuation
│   ├── token_print.of        the `--emit=tokens` format
│   ├── ast.of                the syntax tree
│   ├── parser.of             the token stream, errors, recovery; `parse`
│   ├── parse_*.of            types, patterns, expressions, statements, items
│   ├── ast_text.of           types, patterns and literals as text
│   └── ast_print.of          the `--emit=ast` format
├── sema/
│   ├── program.of            the modules of a program
│   ├── def.of                declarations (`Def`), resolutions (`Res`), locals
│   ├── scope.of              module tables and the binding stack of locals
│   ├── resolve.of            declarations and the passes; `resolveProgram`
│   ├── resolve_import.of     imports, `m.name`, the prelude
│   ├── resolve_body.of       function bodies, patterns, locals
│   ├── trace.of              the `--emit=names` format
│   ├── types.of              the type table (hash-consed `Ty`)
│   ├── check.of              the checker's state, items and function bodies
│   ├── check_types.of        type expressions, signatures, aliases
│   ├── structs.of, enums.of  fields and variants of declarations
│   ├── struct_exprs.of       struct literals, fields, tuple struct constructors
│   ├── variant_exprs.of      building variants
│   ├── expr_check.of         expressions, `coerce`, constants
│   ├── check_stmt.of         statements, blocks, `if`, `match`
│   ├── check_pattern.of      patterns
│   ├── exhaustive.of         exhaustiveness and unreachable arms
│   ├── check_methods.of      method calls, fields, indexing, clones
│   ├── check_generic.of      inference of type arguments
│   ├── check_list.of         lists, maps, iterators, slices
│   ├── check_option.of       `?`, `??`, unused results
│   ├── check_ref.of          references and their escape rules
│   ├── check_box.of          implicit dereferences of boxes and references
│   ├── check_place.of        places and mutability
│   ├── check_unsafe.of       `unsafe`, raw pointers, the C boundary
│   ├── const_eval.of         compile-time evaluation of constants
│   ├── moves.of              move analysis: states, statements, loops (E0600-E0602)
│   ├── moves_expr.of         move analysis of expressions and places
│   ├── moves_match.of        move analysis of patterns, `if const`, `match`
│   ├── borrows.of            borrow checking, non-lexical (E0700-E0705)
│   ├── type_trace.of         the `--emit=types` format
│   ├── check_items.of        declarations, signatures
│   ├── check_exprs.of        expressions and statements
│   ├── infer.of              type inference (unification)
│   ├── interfaces.of         interface satisfaction, vtables
│   ├── generics.of           bounds checking, instantiation
│   ├── patterns.of           pattern typing and exhaustiveness
│   └── consts.of             compile-time evaluation
├── borrow/
│   ├── moves.of              move and initialization analysis
│   ├── borrows.of            borrow checking (non-lexical)
│   └── escape.of             second-class reference rules
├── air/
│   ├── air.of                AIR: operands, statements, blocks, bodies
│   ├── air_print.of          the `main air` format (stage 0's `--emit=air`)
│   ├── lower.of              roots, monomorphization, drop scopes and flags, glue
│   ├── lower_expr.of         expressions, methods, lists, clones, hashes, statements
│   ├── lower_pattern.of      enum values, patterns, `match`, `if const`
│   └── validate.of           AIR invariants checker [planned]
├── opt/                      Ofir-level passes before LLVM [planned]
│   └── inline_small.of
├── codegen/
│   └── llvm.of               AIR → LLVM IR text: types, layout, names, bodies, the module
├── fmt/                      `ofir fmt`
└── lsp/
    ├── index.of              `ofir lsp`: the messages, the documents, diagnostics as LSP
    ├── check.of              checking a file with the open documents
    └── json.of               the JSON parser
```

## `runtime/` — C runtime

```
runtime/
├── include/
│   └── ofir_rt.h            interface used by generated code
├── src/
│   ├── entry.c               process entry, argument capture
│   ├── panic.c               panic reporting and exit
│   ├── mem.c                 global allocator wrapper
│   ├── io.c                  writes and the standard output buffer
│   ├── format.c, str.c       numbers and strings for interpolation
│   ├── os.c                  arguments, environment, exit, running programs (std.os)
│   ├── stack.c               stack overflow detection
│   └── fs.c                  files, directories, standard input (std.fs, std.io)
└── Makefile
```

The runtime is deliberately small. Everything that can be written in
Ofir lives in `std/`.

## `std/` — standard library (Ofir)

Module paths follow directories (spec 7.1): `std/collections/map.of` is the
module `std.collections.map`, and a directory's `index.of` file is the
module named after the directory (`std/fs/index.of` is `std.fs`). The tests
of `x.of` are in `x_test.of` next to it (`std/core/list_test.of`).

```
std/
├── core/                     std.core, the prelude: imported into every module (spec 7.4)
│   ├── index.of              the prelude's own declarations (Ordering) and imports
│   ├── str.of                `@lang("str")`: the methods of str
│   ├── split.of              StrSplit and StrBytes, the iterators of str
│   ├── print.of              eprint and eprintln
│   ├── num/                  i8.of ... u64.of, f32.of, f64.of: the numeric methods, one file per
│   │                         type, generated by tools/gen_core_numbers.py
│   ├── box.of                `@lang("box")`: Box<T>, and the sizeOf<T>() intrinsic
│   ├── list.of               `@lang("list")`: List<T>
│   ├── slice.of              `@lang("slice")`: Slice<T>, the type `&[T]`
│   ├── map.of                `@lang("map")`: Map<K, V>, ordered by insertion
│   ├── hash.of               the deterministic hash functions of Map keys
│   ├── option.of             `@lang("option")`: Option<T>, in scope everywhere
│   ├── result.of             `@lang("result")`: Result<T, E>, in scope everywhere
│   ├── interfaces.of         Eq, Ord, Hash, Clone, Display, Debug, Iterator, ...
│   └── panic.of
├── mem/                      std.mem: replace and swap; take, UnsafeCell, Cell [planned]
├── collections/              std.collections [planned]: sets, deques; List, Map and Box are in std/core
├── strings/                  std.strings: join, fromUtf8, Builder
├── fmt/                      std.fmt: formatting used by interpolation
├── io/                       std.io: IoError, stdin, the runtime's buffers
├── fs/                       std.fs: files and directories
├── os/                       std.os: args, env, exit, run
├── math/                     std.math
├── testing/                  std.testing: assertEq, Bencher, test harness
├── time/                     std.time: monotonic clock, sleep, Deadline
├── task/                     std.task: tasks on OS threads
└── sync/                     std.sync: channels; Mutex, RwLock, atomics, Arc [planned]
```

## `tests/` — test suites

```
tests/
├── README.md                 how to write and run tests
├── runner/
│   └── run_tests.py          test driver (developer dependency only)
├── lex/                      *.of + *.tokens   expected token streams
├── parse/                    *.of + *.ast      expected AST dumps
├── run/                      *.of + *.out      compile, run, compare stdout
├── compile_fail/             *.of with `// error[E0300]: message` annotations
├── panic/                    *.of with `// panic: message` annotations
├── borrow/                   ownership and borrow checker cases
├── codegen/                  *.of + *.ll       FileCheck-style IR checks
└── selfhost/                 stage2 == stage3 verification
```

Every bug fix adds a test. Each test file is small and focused on one
behavior.

## `bench/`, `examples/`, `tools/`

```
bench/
├── programs/                 fib, nbody, binary_trees, spectral_norm, ...
├── baselines/                equivalent C, Rust and Go versions
└── run_bench.py

examples/                     showcase programs, all compiled by CI

tools/
├── bootstrap.sh              build stage0 → stage1 → stage2 from scratch
├── verify_selfhost.sh        compare stage2 and stage3 outputs
├── check_sizes.py            size rules of the style guide (make test)
├── check_docs.py             documentation comments of std, ADR 0020 (make test)
├── check_leaks.py            every run test under valgrind (make check-leaks)
└── gen_core_numbers.py       generates std/core/num/
```

## Dependency rules

```
examples, tests, bench ──► compiler / bootstrap ──► runtime, std
                                │
             driver ─► syntax ─► sema ─► air ─► borrow ─► codegen
                │         ▲        ▲       ▲       ▲         ▲
                └─────────┴─── base, diag (used by all phases) ┘
```

- A phase may depend only on earlier phases and on `base` and `diag`.
- `syntax` knows nothing about types; `sema` never produces LLVM IR.
- `borrow` analyses AIR (a control-flow graph), not the AST.
- `codegen` reads only AIR, never the AST.
- `runtime` must not depend on `std`; `std/core` allocates only through
  the runtime allocator (`ofir_alloc`, for `str`), never through collections.
- Types and methods are documented in the [standard library
  reference](../std/README.md).
