# Changelog

All notable changes to this project are documented in this file. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- The compatibility policy before 1.0 (`docs/design/compatibility.md`,
  ADR 0037): no stability promise; minor releases may break, patch
  releases do not; deprecation with a shim before removal; each break has
  a changelog migration and a migration fixture; `edition` is reserved.
- The auditable bootstrap path in CI: `tools/bootstrap_chain.sh` builds
  every release from 0.5.0 on from its stage 0, each by the one before,
  and checks the current sources against `make verify`; the Bootstrap
  workflow runs it weekly (roadmap 6.9).
- Const generics in stage 1: `const N: int` parameters, used as values
  and as array lengths (`[T; N]`), given as `Buffer<u8, 4>` or inferred
  from an array's length (spec 8.7; E0361, E0362).
- Seeded maps: `Map.seeded()` keys the hash with a random seed from the
  operating system and `Map.withSeed(seed)` with a given one, against
  hash flooding (ADR 0034). String keys now hash with SipHash-1-3.
- Codegen units in stage 1: a program is compiled as one LLVM module per
  Ofir module, with `hidden` and `linkonce_odr` linkage between them;
  `--codegen-units=1` keeps one module, and `--emit=llvm` is unchanged
  (roadmap 6.6c, ADR 0033).
- `-j N` (`--jobs N`): the codegen units are compiled by at most `N`
  clang processes at a time, by default one per core, then linked in one
  more clang call (roadmap 6.6d).
- The object cache: the object of each codegen unit is kept in
  `$OFIR_HOME/cache/objects`, keyed by the unit's text, the clang flags
  and the clang version, and reused by later builds; `OFIR_CACHE_LIMIT`
  bounds it (2 GiB by default) and `--no-cache` turns it off (roadmap
  6.6e, ADR 0033).
- A `--release` build is one codegen unit, cached as one object: units
  made the self-hosted compiler 59% slower (roadmap 6.6f).
- `fs.rename`, `fs.metadata` (size and modification time) and `fs.touch`.
- `os.spawn` starts a program without waiting for it, `Child.wait` waits
  for it, and `os.cpuCount` counts the processors; `os.run` is `spawn`
  followed by `wait`, and the output of a program goes to temporary files
  instead of pipes (roadmap 6.6b, ADR 0033).
- `ofir lsp`, a language server on standard input and output: it publishes
  the diagnostics of each open document as it is opened and changed
  (roadmap 6.3a), and answers go to definition for locals, declarations,
  imported modules and types (roadmap 6.3b), and completes names in scope,
  module members, fields, methods and types (roadmap 6.3c).
- `Stdin.read(n)` in `std.io`: up to `n` bytes of standard input.
- Doc tests: under `ofir test`, the ```` ```ofir ```` blocks of
  documentation comments are compiled and run as tests, `no_run` blocks
  only compiled and `ignore` blocks skipped (spec 12.5, roadmap 6.4a).
  The examples of std and `compiler/` now compile and pass.
- `ofir doc`: an HTML page for each module, with its public declarations,
  their signatures and the Markdown and tags of their documentation
  comments, and an index (roadmap 6.4b).
- Path dependencies: the `dependencies` of `ofir.json` with a `path` are
  read, transitively, and their modules imported by the dependency's key
  (`import json.parser`); `ofir.json` is read as JSONC. Errors E0910,
  E0911 and E0915 (ADR 0032, roadmap 6.5b, stage 1).
- Git dependencies: `{"git": URL, "version": "^1.2"}` (or `rev`,
  `branch`), resolved to one version per package, the highest that every
  semver requirement accepts (E0912); `ofir.lock` records the commits, and
  `ofir fetch` and `ofir update [name]` fetch and resolve them (ADR 0032,
  roadmap 6.5c, stage 1).
- `ofir.lock` records the SHA-256 of each git package's files, and a
  checkout that differs is E0914; `--locked` fails instead of changing the
  lock (E0913) and `--offline` uses only the package cache (ADR 0032,
  roadmap 6.5d, stage 1).
- Registry dependencies: `"json": "^1.2"` (or `{"version": ...}`) is
  resolved from the index of `$OFIR_REGISTRY`, a git repository with a
  line of JSON per published version; yanked versions are used only when
  locked, and archives are downloaded into `$OFIR_HOME/cache` and checked
  against the index's checksum (ADR 0032, roadmap 6.5e, stage 1).
- Labeled loops: `name: for ...`, left from an inner loop with
  `break name` or `continue name`; E0323 and E0324 report a label that no
  enclosing loop has or that one already uses (roadmap 6.7, stage 1).
- Compile-time code, first step: `comptime if` keeps the branch whose
  condition holds for each instance of a generic function, and narrows
  its type parameters with the `std.meta` predicates (`isInteger`,
  `isSigned`, `isFloat`, `isStruct`, `isEnum`, `implements<T, I>`), so a
  numeric `T` takes the operators, literals and casts of its concrete
  type; `meta.nameOf<T>()` names a type and `comptime panic("...")` fails
  the instances that keep it (E0363, E0814; spec 15, ADR 0035, roadmap
  6.7a2, stage 1).
- `comptime for field in meta.fieldsOf<T>()` (or `key in keyof T`)
  expands its body once per field, in declaration order, with
  `field.name`, `field.index`, `field.get(value)` and
  `field.attribute("json")`, the value of a `@tag("json", "...")` on the
  field; `fieldsOf` needs a struct (E0364), and the unrolled loop has no
  `break` or `continue` (spec 15.3, roadmap 6.7a3, stage 1).
- `comptime for variant in meta.variantsOf<T>()` expands its body once
  per variant of an enum, with `variant.name`, `variant.index`,
  `variant.attribute("json")` (from a `@tag` on the variant) and
  `variant.matches(value)`, tested at run time (spec 15.3, roadmap 6.7a4,
  stage 1).
- `comptime f(...)` inside a function runs `f` in the compiler, by an
  interpreter of the AIR, and builds its value into the program: a
  number, `bool`, `str`, array, tuple, struct or enum (E0818). A
  `comptime fn` runs only there (E0817); I/O, `extern` calls and
  run-time values are E0815, and a panic, an arithmetic or bounds error
  or a spent step budget (10 million, `@comptimeBudget(n)`) is E0816
  (spec 15.5, roadmap 6.7a5, stage 1).
- `const X = comptime f()` at module level: the interpreter computes the
  constant, which may be an array, tuple, struct or enum. It cannot be
  used in another constant or an array length (E0801; spec 15.1, roadmap
  6.7a6, stage 1).
- Heap allocation in `comptime` code: `List`, `Map`, `Box` and `str`
  work there as at run time, and `clone` copies them deeply (spec 15.5,
  roadmap 6.7a7, stage 1).
- A faster `comptime` interpreter: ten million steps now take about 2.5
  seconds instead of 15 (roadmap 6.7a8, stage 1).
- Mapped types over type parameters: `fn merge<T>(base: move T, patch:
  move Partial<T>) T` merges a patch field by field with `comptime for`
  (spec 16.6, roadmap 6.7b1, stage 1).
- The standard aliases `Partial`, `Required`, `Pick`, `Omit`, `Record`
  and `Unwrap` in the prelude (`std.core.types`, spec 16.7, roadmap
  6.7b2, stage 1); stage 0 loads them as opaque aliases.
- `Pick<T, K>`, `Omit<T, K>`, `if` filters and conditional types such as
  `Unwrap<T>` over type parameters, decided at each instantiation (spec
  16.4-16.6, roadmap 6.7b3, stage 1).

### Changed

- A type that declares `eq` no longer derives `Ord` (stage 1): `<` and
  `sort` must agree with the declared equality. Declare `cmp` as well, or
  compare a key field instead (spec 9.9, OFIR-05).
- Stage 1 computes the layout of each struct and enum once per module it
  generates; the layout of nested enums took exponential time, and a
  self-compile spent most of its code generation there.
- `let` declares a mutable binding and `const` an immutable one, in
  functions and at module level; `if let` and `let ... else` are now
  `if const` and `const ... else`. `var` is no longer a keyword (ADR 0031).

### Fixed

- The parts of a consumed value that a pattern does not bind (a `_`, a
  field left out by `..`, the value matched in the `else` branch of an
  `if const` or of a `const ... else`) are dropped instead of leaked, also
  when the arm or branch is left by `return` or `break`.

## [0.5.0] - 2026-11-06

Milestone M5: every chapter of the specification not marked [planned] is
implemented and tested (roadmap phase 5 and the M5 check, 5.22a-g). Most of
the new language is implemented in stage 1 only; stage 0 stays the frozen
bootstrap subset.

### Added

- Returning borrows and `ref struct` (roadmap 5.1): `List.iter()` is written
  in Ofir, and `for` accepts iterators of generic types.
- Generic bounds and `where` clauses (5.2); interfaces with structural
  satisfaction, assertions, default methods and embedding (5.3); `dyn I`
  with vtables and fat pointers (5.4); type switches, `Any` and `typeName`
  (5.4b); interfaces as parameter types (5.4c).
- Built-in interfaces derived automatically: `Eq`, `Ord`, `Hash`, `Clone`,
  `Debug` and `{x:?}`; `From` in `?` (5.5).
- `List`, `Map`, `Option`, `Result` and `Box` as generic Ofir code in `std/`
  (5.6).
- Closures with arrow syntax, `fn` and `move fn` types and captures (5.7);
  the iterator interface and `for` over iterators (5.8); `UnsafeCell` and
  `Cell` (5.9).
- Format specifiers `{x:spec}`, raw, multi-line and byte strings (5.10).
- Attributes checked in both stages (E0807-E0810), `@inline`, `@cold`,
  `@deprecated` (W0006), `@allow`, `@cfg`, `@repr(C)`, `@packed`,
  `@align(N)`, `@export` and `@link` (5.11a-e).
- Default parameter values and named arguments (5.12a);
  `--message-format=json` (5.12b); notes and help in diagnostics (5.12c);
  `ofir explain` with an explanation file for every code (5.12d); "did you
  mean" suggestions (5.12e).
- Debug info: `-g` emits DWARF functions and line tables (5.13).
- Optimizations: `noalias`/`readonly` parameters with `--release`, niche
  layouts for `Option<&T>`, `Option<Box<T>>` and `Option<fn>`, struct field
  reordering and AIR check elimination (5.14).
- A generated-code benchmark suite with C, Rust and Go baselines (5.15).
- Generic type aliases (5.16); key types with `keyof`, key literals and
  key-bounded parameters (5.17); mapped and conditional types (5.18).
- Union types with widening, narrowing in `match` and `if let`, and the
  built-in interfaces (5.20, 5.21, 5.21b).
- Fixed arrays `[T; N]` (5.22a), `T?` for `Option<T>` (5.22b), mutable
  slices `&mut [T]` (5.22c), mutable globals (5.22d), `str.toCString()`
  (5.22e), and `@bench` with `ofir bench` (5.22f).

## [0.2.0] - 2026-11-03

Milestone M4: the compiler is self-hosted. The first seed,
`ofir-0.2.0.ll`, is attached to the release (`docs/bootstrap.md`).

### Added

- Stage 1 (roadmap phase 4): `compiler/base`, `compiler/diag`, the lexer
  and the parser, whose tokens and trees equal stage 0's on every source,
  `compiler/` included (milestone M3, `make compare-stages`); a project
  root is the nearest directory with an `ofir.toml`.
- Stage 1 module loading and name resolution (roadmap 4.5), checked against
  stage 0's new `--emit=names` trace by `make compare-stages`.
- `fs.canonicalize`.
- Stage 1 type checker (roadmap 4.6), checked against stage 0's new
  `--emit=types` trace by `make compare-stages`.
- Stage 1 move analysis (roadmap 4.7); `--emit=types` now runs the move
  analysis, so its errors are compared too.
- Stage 1 AIR and lowering (roadmap 4.7): `main air` equals stage 0's
  `--emit=air` on every program of the repository, stage 1 itself
  included; drops, clones, hashes and monomorphization included.
- Stage 1 LLVM codegen (roadmap 4.8): `main llvm` equals stage 0's
  `--emit=llvm`; `main build` and `main run` compile programs with clang,
  and the run and panic tests pass with stage 1 (`make test-stage1`).
- Stage 1 driver (roadmap 4.9): stage 0's command line, `ofir test`
  included; every test passes with stage 1 as the compiler.
- Self-hosting (roadmap 4.13): stage 1 builds itself (stage 2), which
  builds itself (stage 3); `make verify` checks that both write the same
  LLVM IR for `compiler/`.
- Stage 1 borrow checker (roadmap 4.10): shared and exclusive borrows of
  places are checked against moves, writes and each other, with
  non-lexical borrows and two-phase borrows (E0700, E0701, E0703 to
  E0705). The compiler passes its own borrow checker (roadmap 4.12).
- Stage 1 escape rules (roadmap 4.11): a borrowed `fn(A) R` cannot be
  stored or returned (E0702), and a borrow cannot outlive the block of
  what it borrows (E0706).
- Release seeds (roadmap 4.14): `make seed` writes the LLVM IR of the
  compiler, `make check-seed SEED=...` builds a compiler from one and
  tests it; the Release workflow publishes a release with its seed.
- `checkedAdd`, `checkedSub`, `checkedMul`, `checkedDiv` and `parse` for
  the integer types, `parse` for the float types, `ParseError`.

### Fixed

- A reference to a binding of a `match` could be returned from a function
  (`return match x { .A(v) => &v }`): it pointed to a copy that ends with
  the function (E0702).
- A type path with an unknown module (`nowhere.T`) was an error type
  without a message (E0200).

- Matching the value in a box bound its parts by move and freed them twice.
- A `let` in a loop kept the parts moved out in the previous iteration.
- `--emit=ast` crashed on a tree with a missing part after a syntax error.
- Stage 1's parser looped forever on a `.` at the end of the file (`x.`).
- Stage 0 left `deny_warnings` uninitialized, which turned warnings into
  errors on macOS.

### Changed

- The language is renamed Ofir (ADR 0025): source files end in `.of`, the
  compiler is `ofir`, the manifest `ofir.toml`, the environment variables
  `OFIR_*` and the runtime symbols `ofir_*`.
- The manifest is `ofir.json`, JSON with comments and trailing commas
  (ADR 0026), instead of `ofir.toml`.
- A directory's module file is `index.of` (was `mod.of`).
- Tests live in a `name_test.of` file next to the module, part of the
  module; only `ofir test` loads it. The std tests moved there.
- Style: no inline blocks (`"fmt": {"inlineBlocks": false}`). Function and
  statement bodies always go on their own lines; `make check-format`
  (`tools/check_format.py`, with `--fix`) enforces it in `make test`.

## [0.1.0] - 2026-11-01

Milestone M2: stage 0 is complete.

### Added

- Generics, `Box`, `List`, slices and references, `Option`/`Result` with
  `?` and `??`, `Map`, exhaustive `match`, warnings (phase 2).
- Ownership: move analysis, drops with drop flags and glue, `.clone()`
  derived for structs, enums and tuples (phase 3).
- `for x in iterator`; `str` splitting, searching and trimming;
  `std.strings`, `std.io`, `std.fs` and `std.os`; `eprint`/`eprintln`.
- Stack overflow detection in the runtime.
- `@test` functions and `ofir test`; std tests run in CI.
- Generated-code benchmarks against C (`bench/run_programs.py`).
- Memory checks: `make check-leaks` (valgrind), `make check-asan`, and a
  mutation fuzzer for the compiler.

### Earlier

- Language specification draft (`docs/spec`), standard library reference
  (`docs/std`), architecture documents and decision records.
- Implementation roadmap and risk register.
- Project skeleton, build system and test runner.
- Stage 0 compiler (C): lexer, parser, name resolution, type checker, AIR,
  lowering, LLVM IR generation and the `build`/`run` commands (milestone M1).
- Runtime library: entry point, panics, buffered standard output, strings.
- Benchmarks for every phase (`bench/`) with a regression gate.
