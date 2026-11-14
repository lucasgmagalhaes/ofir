# 0073. Coverage through gcov counters and profiling through DWARF

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-61 asks for profiling and coverage: results that point at the Ofir
source through generics and inlining, export formats other tools read,
and a measured overhead.

The compiler emits LLVM IR and builds it with clang. LLVM has two
coverage schemes. Source-based coverage (`-fprofile-instr-generate
-fcoverage-mapping`) needs the front end to write a coverage map of
regions, which the Ofir compiler would have to build. gcov coverage
(`--coverage`) instruments the IR in clang and maps the counters to the
source through the line table, which `-g` already writes. Sampling
profilers (`perf`, callgrind, Instruments, VTune) only need symbols
and DWARF.

## Decision

1. **Coverage: `--coverage`.** The flag (stage 1) implies `-g`, passes
   `--coverage` to each clang call and turns the object cache off,
   because the counters' files are named after the objects. A run
   writes a `.gcda` next to each `.gcno`; `llvm-cov gcov` (or `gcov` on
   files from a matching GCC) reports per line, and `gcovr` or `lcov`
   turn the counters into HTML, Cobertura XML or LCOV. Counts add up
   across runs, so a test suite's runs merge.
2. **Through generics and inlining.** Every instance of a generic
   function carries the lines of its declaration, so the counts of its
   instances add up on those lines. Inlined code keeps the line of its
   source, so `--release --coverage` counts it where it was written.
3. **Profiling: the existing tools.** A program built with `-g` (and
   `--release -g` to profile optimized code) has symbols named after
   the source (`ofir.main`, `ofir.twice<int>`) and DWARF lines, which
   `perf`, `valgrind --tool=callgrind` and other profilers read. Ofir
   ships no profiler.
4. **Checked.** `tools/check_profile.py`, run by `make test-stage1`,
   checks the counts of a program with a generic function and an
   unexecuted branch, that a second run adds to them, and that
   callgrind names the functions and their file; it prints the
   overhead it measures. It is skipped where clang has no profile
   runtime (`libclang-rt-dev` on Debian and Ubuntu), `llvm-cov` or
   valgrind is missing, and on Windows.

## Consequences

- Coverage needs no compiler-specific format and no new front-end pass;
  its granularity is the line, not the region: a line with a branch
  that ran one way counts as executed.
- The closing brace of a function whose body ends in `return` is
  reported as a line that never ran, as in C.
- Measured on the check's loop at `--release`: coverage costs about
  2x, callgrind about 50x (the check prints its own numbers).
