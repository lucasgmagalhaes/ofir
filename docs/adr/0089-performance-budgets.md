# 0089. Performance budgets

- Status: Proposed
- Date: 2026-11-19

## Context

OFIR-75 asks for budgets on compile time and memory, binary size,
startup, cold and warm builds and the runtime of generated programs, and
for the existing benchmark gate to stay.

What exists: `make bench-compare` fails a pull request whose compiler
benchmarks execute more than 3% more instructions than the base branch.
It is relative, so a slow drift of 2% per change passes forever, and it
covers only stage 0 on generated inputs. `make bench-programs` compares
the generated programs with C, Rust and Go and gates nothing. The targets
in `docs/design/performance.md` were written down but never checked.

## Decision

1. `bench/budgets.json` holds absolute ceilings, measured on Linux
   x86_64 with clang 18 (CI's `ubuntu-24.04`):
   - compiler peak RSS per source byte, the largest of `check-100k` and
     `llvm-100k` with the stage 0 release build;
   - cold and warm self-compile of `compiler/main.of` with stage 1 in an
     empty `OFIR_HOME`, and the number of units the warm build compiles;
   - the stripped size of `examples/hello.of` built with `--release`,
     the instructions it executes, and its startup wall time (p90);
   - the instructions each program in `bench/programs` executes when
     built with `--release`, with its wall time distribution (min, p50,
     p90, max).
2. A ceiling sits about 10% above the measured value, except where a
   target in the design document is tighter, which is then the ceiling.
3. Only deterministic metrics gate: instruction counts, sizes, unit
   counts and the memory ratio. Wall times report, except the warm
   self-compile, whose 5 s target is three times the measured 1.5 s.
   On another platform every budget reports.
4. `tools/perf_budgets.py` (`make check-budgets`) prints one line per
   budget and writes JSON. A metric whose tool is missing (valgrind,
   strip) prints `unavailable (reason)` and fails under `--require`, as
   in ADR 0087. CI runs it with `--require` in the Performance workflow.
5. The 3% relative gate stays. Lowering a ceiling after an
   optimization, or raising one, is a change to `bench/budgets.json`
   reviewed like code, with the measurement in the pull request.

## Consequences

- A regression that the relative gate lets through in small steps is
  caught once it crosses a ceiling.
- Compiler memory was 22.4× the source, over the 20× target, with a
  ceiling of 24×; streaming the tokens and spilling the LLVM globals
  brought it to 17.8×, and the ceiling is now the 20× target.
- The check takes about a minute: the cold self-compile and the programs
  under valgrind.
- Instruction counts depend on clang's version, so a new CI clang may
  need new ceilings.
