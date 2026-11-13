# 0051. Layout and ABI contracts, checked against C

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-30 asks for the layout and ABI rules to be verified: niches, enums,
zero-sized types, `@repr(C)` and the LLVM attributes, with cross-language
layout and call fixtures and tests that optimization keeps them.

Until now the tests checked layouts only from inside Ofir. Checking them
against C showed where chapters 11 and 13 promised more than the compiler
does:

- `@repr(C)` structs and `extern "C" fn` types were listed as types that
  cross the C boundary. The checker rejects a struct by value (E0804) and
  the parser has no `extern "C" fn` type.
- A `@repr(C)` enum is a C `int` in memory, but it cannot be passed by
  value either.
- An enum without payloads was said to use the smallest tag that fits.
  Both stages give every tagged enum an `i32` tag, and a payload made of
  8-byte units: `Option<u8>` takes 16 bytes.
- `&str` was listed as a pointer and a length. It has the representation
  of `str` (24 bytes), with capacity 0 (spec 2).

## Decision

1. **Run tests can build C fixtures.** A `// c: file.c` line compiles the
   C file next to the test with the clang the driver uses, into
   `lib<file>.a`; the test links it with `@link("<file>")`.
2. **The contracts are checked against C:**
   - `tests/run/ffi/c_calls.of`: every integer width, both float widths
     and `bool` both ways, twelve mixed arguments (some on the stack),
     and pointers that C writes through.
   - `tests/run/ffi/c_layout.of`, built with `--release`: C compares the
     size, alignment and field offsets of `@repr(C)`, `@packed`,
     `@align(N)` structs and a `@repr(C)` enum with its own, reads what
     Ofir wrote and writes fields back.
   - `tests/run/layout/sizes.of`: the sizes and alignments of spec 13.1
     to 13.4, niches and zero-sized types included.
   - The LLVM attributes stay checked by `tests/codegen/noalias.of` and
     `tests/codegen/niche.of`.
3. **The specification states what is implemented.** Chapter 11 lists the
   types that cross today: integers, floats, `bool` and raw pointers. A
   `@repr(C)` struct or enum crosses behind a pointer.
4. **Passing `@repr(C)` structs and enums by value, and `extern "C" fn`
   types, are planned.** The design: the driver generates a small C file
   with one wrapper per such function, each taking and returning
   pointers, and clang compiles it for the target. Clang then applies the
   target's C rules for structs (registers, hidden pointers, alignment);
   the Ofir side only ever passes pointers. The generated LLVM IR stays
   free of target triples (ADR 0033).
5. **Compact enum layouts are planned.** A tag of the smallest integer
   that fits, and a payload aligned to its own fields rather than to 8
   bytes, would make `Option<u8>` 2 bytes instead of 16. AIR reads the
   tag as an `i32` field in both stages, so the change gets its own
   roadmap row and benchmark.

## Consequences

- Programs can rely on the layouts the fixtures check, on all three CI
  systems, with and without `--release`.
- Calling a C function that takes or returns a struct by value needs a
  C wrapper written by hand until the generated wrappers exist.
- Enums with tags use more memory than they need. Version 1 does not
  guarantee their layout, so compacting them breaks no program.
