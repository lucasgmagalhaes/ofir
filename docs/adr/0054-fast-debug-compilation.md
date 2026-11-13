# 0054. Debug builds keep clang and schedule linearly

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-38 asks whether a fast debug backend or a JIT is worth having, and
to decide from a profile, not by hand.

A profile of building the compiler itself (`compiler/main.of`: about 48k
lines, 21 MB of LLVM IR, 3727 functions; 4 cores, clang 18):

| Build | Time |
|-------|------|
| cold debug | 17.3 s, of which clang 15.7 s |
| warm (every object cached) | 1.35 s |
| one function edited in `driver/explain.of` | 1.5 s |
| one function edited in `sema/check.of` | 3.0 s |
| `ofir check` | 0.39 s |
| hello world, cold | 70 ms |

The front end (modules, resolve, check, lower, codegen) takes about
1.2 s. The rest of a cold build is clang at `-O0`, and 71% of that is
instruction selection, in which the pre-register-allocation scheduler
alone takes 9.3 s of 30 s of single-threaded work. FastISel, the fast
`-O0` selector, gives up on 3693 of the 3727 functions: Ofir passes and
loads aggregates (`str`, structs) by value and uses `fastcc`, which it
does not handle, so the slow selector runs. In a synthetic module of
3000 functions, the same code with aggregates behind pointers and the C
calling convention compiles 3 times faster, with no fallback.

## Decision

1. **No alternative backend or JIT.** The object cache already makes an
   edit-rebuild cost 1.5-3 s, and `ofir check` answers in under half a
   second. A second backend would need its own parity with the language's
   semantics (checks, drops, layout, debug info) for a gain only on cold
   builds. One is reconsidered only on a profiled need, and only with a
   parity suite against the clang path.
2. **Debug code generation uses `-mllvm -pre-RA-sched=linearize`.** It
   orders instructions as they come instead of scheduling for register
   pressure, which at `-O0` changes nothing the program can observe and
   makes clang about 40% faster on Ofir's large blocks. Release builds
   keep the default scheduler. The flag is part of the object cache key,
   and is not passed to a call that only links.
3. **The next lever is the shape of the IR**: large aggregates passed by
   pointer and the C calling convention in debug builds, so FastISel
   applies. It changes code generation in both stages and is left for when
   cold builds matter more than that cost.

## Consequences

- A cold debug build of the compiler goes from 17.3 s to 11.6 s; warm and
  edit-rebuild times are unchanged.
- Debug objects compiled before this change miss the cache once.
- The flag is an LLVM option, not a clang one: a clang whose LLVM drops
  it would fail every debug build, and the flag would then be removed.
- Stage 0 keeps the default scheduler; it compiles only the bootstrap.
