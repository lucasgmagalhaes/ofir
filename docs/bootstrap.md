# Bootstrap plan

The goal is a **self-hosted** Ofir compiler: the compiler is written in
Ofir and compiles itself. A compiler cannot compile itself before it
exists, so we get there in stages.

## Stages

```
stage0  bootstrap compiler, written in a host language
          │ compiles
          ▼
stage1  Ofir compiler written in Ofir, built by stage0
          │ compiles
          ▼
stage2  Ofir compiler built by stage1
          │ compiles
          ▼
stage3  Ofir compiler built by stage2   →  must be identical to stage2
```

### Stage 0 — bootstrap compiler

- Written in **C11** (see [ADR 0003](adr/0003-self-hosting-with-c-bootstrap.md)).
- Implements only the subset of Ofir needed to write the real compiler
  (see below). Error messages can be basic; optimizations come from LLVM.
- Lives in `bootstrap/`. It is frozen once stage 1 works and is only
  changed when the self-hosted compiler needs a new feature to build itself.

### Stage 1 — self-hosted compiler

- Written in Ofir, in `compiler/`.
- Implements the full specification, with high-quality diagnostics.
- Built first by stage 0, then by itself.

### Verification

Stage 2 and stage 3 must produce byte-identical output. This proves the
compiler compiles itself correctly and is checked in CI.

### After M4: the bootstrap chain

Once self-hosted, the compiler may start using language features that
stage 0 does not support. To keep the project buildable from scratch, a
feature may be used in `compiler/` only **after it has shipped in a tagged
release**, so release *N* is always compilable by release *N − 1*.

Each release publishes a **seed**: the LLVM IR that the release compiler
generates for itself (`ofir-<version>.ll`), attached to the release, with
its SHA-256 checksum recorded in `seed/SEEDS.md`. The IR has no target
triple, so one seed serves every 64-bit target clang supports; it records
the source paths of the machine that built it, for panic messages. There
are two ways to build the current compiler:

1. **Fast path (normal):** download the seed of the previous release, compile
   it with clang, and use that compiler to build the current sources:
   `make check-seed SEED=ofir-<version>.ll` does this and runs the tests
   with the result.
2. **Auditable path:** stage 0 → the first self-hosted release → every
   following release in order. It requires only a C compiler and clang and
   proves that no seed hides anything ("trusting trust"). Running it in CI
   on a schedule is planned.

`make seed` writes the seed of the current sources
(`build/seed/ofir-<version>.ll` and its `.sha256`), after `make verify`.
The Release workflow (`.github/workflows/release.yml`, run by hand with the
version) builds the seed, checks it with `make check-seed` on every CI
platform, and creates the tag `v<version>` and the release with the seed.

Stage 0 stays in the repository, frozen, as the root of the auditable path.

## Compiler pipeline

Both compilers share the same architecture, described in
[design/compiler-architecture.md](design/compiler-architecture.md):

```
source (.of)
  → lexer            tokens
  → parser           AST
  → resolver         names bound to declarations, modules loaded
  → type checker     typed AST
  → lowering         AIR: control-flow graph
  → borrow checker   moves, borrows, definite initialization
  → drops            explicit drops and drop flags
  → mono             concrete instances of generic code
  → codegen          LLVM IR (textual .ll)
  → clang            optimization, machine code, linking with the runtime
```

Generating **textual LLVM IR** instead of linking against the LLVM
libraries keeps the compiler free of C bindings and easy to self-host. The
generated code is optimized by exactly the same LLVM passes either way
(see [ADR 0001](adr/0001-textual-llvm-ir-backend.md)).

## Runtime

A small C library (`runtime/`) provides what LLVM IR cannot express
portably: memory allocation, process entry, panics and system calls. It is
linked into every program and will shrink over time as the standard library
moves to Ofir. See [design/runtime.md](design/runtime.md).

## Bootstrap subset

Stage 0 supports only what the self-hosted compiler needs. Until milestone
M4, the code in `compiler/` must be written in this subset.

**Included:**

- functions, `const`/`let`, `const`, all integer types, `float`, `bool`
- arithmetic (checked), comparisons, logic, bitwise operators, `as`
- `if`, `for` (all forms), `break`, `continue`, `return`
- `str`, string interpolation (without format specifiers), `print`/`println`
- structs and enums with methods and associated functions declared in
  their body, `copy struct`, default field values, tuple structs
- enums with data, `match` (exhaustiveness checked), `if const`,
  `const ... else`, the `??` operator
- tuples
- `==` and `!=` on integers, floats, `bool`, `str` and fieldless enums
  (derived equality for other types comes with interfaces in stage 1)
- parameter modes: borrow (default), `mut` and `move`, for parameters and
  `self`; `for mut` loops; `[T]` and `str` parameters accepting slices
- explicit references `&T`, `&mut T` as locals; slices `&[T]`, `&str`
- built-in generics: `List<T>`, `Map<K, V>`, `Option<T>`, `Result<T, E>`,
  `Box<T>`, and the `?` operator (both error types must be equal)
- user-defined generic functions, structs and enums (monomorphized),
  with the bounds `Eq`, `Ord` and `Hash` on their type parameters and in
  `where` clauses, also on methods: stage 0 compares and hashes scalars,
  `str`, pointers, enums without payloads and tuples of these; a bound
  that a struct or enum would satisfy through derived code, or any other
  bound, is a stage 0 limit (E0901)
- `list.sort()` (`where T: Ord`) for lists of integers, floats and `str`
- built-in methods that return borrows, special-cased by the compiler:
  `List.get`, `getMut`, `first`, `last`, `Map.get`, `getMut`, and the
  iterators `str.split`, `splitWhitespace`, `lines`, `bytes`, which may be
  bound locally or used directly in `for` loops
- moves and automatic drops
- modules, `import`, `pub`
- `extern "C"` blocks and `unsafe` blocks
- `@test` functions, `@inline`, `@inline(never)` and `@cold`; every
  attribute is checked (spec 12.1); `@deprecated` and `@allow` are
  accepted and ignored

**Excluded** (implemented only in stage 1):

- the borrow checker (stage 0 checks moves, not borrows)
- bounds other than `Eq`, `Ord` and `Hash`, interfaces and `dyn`
- closures and function values
- `ref struct` and user functions returning borrows
- error conversion in `?`
- format specifiers, raw strings, attributes other than `@test`, `@inline`
  and `@cold`; the warnings of `@deprecated` (W0006) and the silencing of
  `@allow`
- default parameter values and named arguments
- compile-time code (`comptime`, `std.meta`)
- key, mapped and conditional types (stage 0 parses an alias whose body is
  a mapped or conditional type, as in the prelude, and rejects a use of it)
- good diagnostics: stage 0 reports the first error per function

Programs accepted by stage 0 are still valid Ofir: stage 1 enforces every
rule when it compiles itself. To make this true, **the stage 1 borrow
checker is part of milestone M4**: self-hosting is only declared once the
compiler's own source passes the full ownership and borrowing rules.

Because stage 0 does not check borrows, it must **never** emit `noalias` or
`readonly` attributes: on a program with aliasing `mut` arguments they
would turn an undetected mistake into undefined behavior. Stage 0 output is
correct for every program it accepts, only less optimized.

## Milestones

| # | Milestone | Done when |
|---|-----------|-----------|
| M0 | Specification draft | `docs/spec` reviewed |
| M1 | Hello world | stage 0 compiles and runs `examples/hello.of` |
| M2 | Bootstrap subset | stage 0 passes every test in `tests/` not marked `requires: stage1` |
| M3 | Self-hosted front end | stage 1 lexes and parses its own source |
| M4 | Self-hosted | stage 1, with its borrow checker enabled, compiles itself and stage2 == stage3 |
| M5 | Full language | every chapter of the specification is implemented |

The detailed task list is in [roadmap.md](roadmap.md).
