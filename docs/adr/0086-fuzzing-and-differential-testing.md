# 0086. Fuzzing, properties and differential testing

- Status: Proposed
- Date: 2026-11-19

## Context

OFIR-73 asks for the existing fuzzing tools to be extended, with
minimized regression seeds for the parser, the checker and the optimizer.

What exists: `tools/fuzz_frontend.py` (roadmap 3.12) mutates the sources
of the repository and requires diagnostics, never a crash, hang or
sanitizer report; `tools/compare_stages.py --fuzz 200` gives the same
mutants to both stages and requires the same output. Neither was run with
a fixed set of past failures, a failing input was kept whole, and nothing
checked that a program which compiles also computes the right answer:
the AIR fold, accumulation (ADR 0044) and clang `-O2` under `--release`
could change a result without any test noticing.

## Decision

1. `tools/fuzz_programs.py` generates well-typed programs over the
   integer types `i8`, `i32`, `i64`, `u8`, `u32` and `u64`: arithmetic,
   bit operations, shifts, comparisons, `wrapping*`, `min` and `max`,
   branches and loops. A Python oracle evaluates each program with the
   language's rules (checked arithmetic, `/` truncating toward zero, `%`
   with the dividend's sign, left-to-right evaluation, spec 4.10). The
   compiler must print the oracle's output, or panic with the oracle's
   message (status 101), in four modes: stage 0 and stage 1, each in dev
   and release. That is a property (the program's meaning) and a
   differential test (stages and optimization levels) at once.
2. About a third of the programs end in a panic, so that the checks
   folding or the optimizer could remove are exercised as well.
3. A failing program is minimized: the first failing call is kept with
   its function, loops are dropped and expressions are replaced by
   smaller ones while the failure remains.
4. `tools/fuzz_frontend.py --minimize` shrinks a crashing input by delta
   debugging, first over lines and then over characters. Mutations add
   checker-oriented tokens (types, borrows, generics, patterns, impls).
5. Regression seeds:
   - inputs that once crashed a stage go to `tests/fuzz/frontend/`; the
     fuzzer compiles every seed before mutating, and none may crash;
   - programs that once computed a wrong result go to `tests/run/fuzz/`
     or `tests/panic/fuzz/`, as ordinary tests for both stages.
   The seeds in `tests/fuzz/` are malformed on purpose, so the format
   check and the stage comparison skip them.
6. `make check-fuzz`, part of `make test`, replays the seeds and runs the
   fuzzers with fixed seeds and small counts (about 12 seconds), so a
   failure reproduces. Longer runs use other `--seed` values.

## Consequences

- A miscompilation by fold, accumulation or `-O2` in integer code, or a
  disagreement between stages on it, fails CI with a small program.
- The oracle covers integers only; floats, strings, collections and
  ownership stay with the hand-written tests, and can be added to the
  generator later.
- The first runs (300 programs in four modes, 2 000 mutated inputs on
  both stages) found no failure, so the initial seeds are hand-made
  edge cases and generated programs kept as regressions.
