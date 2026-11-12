# Ofir Language Specification

Version: **0.1 (draft)**

This specification describes the Ofir language. Sections marked
**[planned]** are part of the design but are not required for the first
(bootstrap) implementation.

## Chapters

1. [Lexical structure](01-lexical.md)
2. [Types](02-types.md)
3. [Declarations](03-declarations.md)
4. [Statements and expressions](04-statements-expressions.md)
5. [Ownership and borrowing](05-ownership.md)
6. [Error handling](06-errors.md)
7. [Modules](07-modules.md)
8. [Generics](08-generics.md)
9. [Interfaces](09-interfaces.md)
10. [Closures](10-closures.md)
11. [Unsafe code and FFI](11-unsafe-ffi.md)
12. [Attributes, testing and documentation](12-attributes-testing.md)
13. [Memory layout and ABI](13-memory-layout.md)
14. [Concurrency](14-concurrency.md): tasks, `Send`/`Sync`, channels, `Mutex`, `RwLock`, atomics, `Arc` and `WaitGroup`
15. [Compile-time code](15-compile-time.md): `comptime`, `std.meta` and the AIR interpreter
16. [Type operators](16-type-operators.md): `keyof`, `T[K]`, mapped and conditional types
17. [Union types](17-union-types.md): `A | B` as anonymous tagged enums

Appendices:

- A. [Grammar](appendix-a-grammar.md)

Related: the [standard library reference](../std/README.md) and the
[error code registry](../errors/README.md).

## Design principles

1. **One obvious way.** Prefer a single, simple construct over several
   overlapping ones (e.g. a single `for` loop, as in Go).
2. **Safe by default, fast by design.** Every safety feature must compile to
   zero or near-zero runtime cost. Ownership enables the compiler to tell LLVM
   that `&mut` references never alias (`noalias`).
3. **No hidden control flow.** No exceptions, no implicit allocations behind
   operators other than those documented, no garbage collector.
4. **No null.** Absence is expressed with `Option<T>`.
5. **Explicit mutability.** Bindings are immutable unless declared with `let`.
6. **Readable errors.** Every compile error points to a line and column and
   explains how to fix it.

## Notation

Grammar rules use EBNF:

- `A B` — sequence
- `A | B` — alternative
- `[ A ]` — optional
- `{ A }` — zero or more repetitions
- `"x"` — literal token
