# 0087. Sanitizers, leaks, races and fault injection

- Status: Proposed
- Date: 2026-11-19

## Context

OFIR-74 asks for the unsafe and resource boundaries to be exercised, and
for checks that cannot run to say so instead of passing.

What exists: `make check-leaks` runs every run test and example under
valgrind, and `make check-asan` builds them with AddressSanitizer and
UBSan. Both skipped silently when their tool was missing, nothing ran the
concurrent programs under a race detector, and no test made an
allocation or a read fail: the paths that handle out of memory and I/O
errors in the runtime and in std were never run.

## Decision

1. `tools/safety.py` probes each check by building and running a small
   C program with its flags (or by finding valgrind) and prints which
   checks this machine can run (`make check-safety`). Every check prints
   `unavailable (reason), skipped` when it cannot run, and fails under
   `--require`; CI on Linux passes `--require`
   (`SAFETY_FLAGS=--require`), so a missing tool there is an error.
2. `make check-tsan` builds the programs that import `std.task` or
   `std.sync`, and the runtime, with ThreadSanitizer. A program must give
   its expected output (or panic, for a panic test) and no race report.
3. `make check-faults` loads `tools/fault/inject.c` with `LD_PRELOAD`
   into every run test and example, with the pool off
   (`OFIR_SYSTEM_ALLOC`) so that every block comes from malloc. After a
   run that counts the allocations and the I/O calls:
   - `eintr` (every read and write interrupted once) and `short` (writes
     of one byte) must not change the output or the exit status;
   - `alloc:N` (the Nth allocation and every later one fail) must end as
     the normal run does, or with a panic (status 101 and a message);
   - `io:N` (the Nth read, write or open fails with `EIO`) must end with
     an exit status, never a signal or a hang.
   N is the first, the middle and the last call. The injector interposes
   glibc, so the check is unavailable elsewhere.
4. `check-leaks` reports a program killed by a signal, unless the test
   says it aborts (`// status: abort`).
5. ThreadSanitizer does not model fences, so `Arc`'s drop synchronizes
   with an acquire load of the counter instead of an acquire fence, as
   Rust's does under TSan. The load runs once, when the last reference
   goes; the benchmarks do not change.

## Consequences

- An unchecked allocation, a lost error from a read or write, or a data
  race in the runtime or std fails CI with the program and the fault.
- The first runs found:
  - `read_line` crashed when `getline` ran out of memory; it now panics
    with "out of memory";
  - the pool's spin lock could keep its holder from running when task
    threads outnumber the cores (a hang under TSan); it now yields;
  - a false race report on `Arc`, fixed by point 5.
- Faults are injected at one place per kind and run; a failure that
  needs two faults, or a fault in the middle of a system call that the
  shim does not interpose, is not covered.
- macOS and Windows run the tests, not these checks; they report
  themselves unavailable there.
