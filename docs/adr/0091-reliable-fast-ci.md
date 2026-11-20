# 0091. Reliable, fast CI

- Status: Proposed
- Date: 2026-11-20

## Context

OFIR-77 asks for a CI that is fast and that can be trusted: safe
caching, sharding and parallelism, flake tracking and the logs of a
failed run kept, without losing coverage.

Before this change every CI job ran `make test` as one serial chain. On
Linux that took 9 minutes, then the cross tests and the safety checks
took 3 more; macOS took 12 and Windows 9.5. A new push to a pull request
did not cancel the run of the push before it, no job had a time limit,
and a failed job kept nothing but its console log.

A test could also hang the whole run. The test tools let a test inherit
the runner's standard input and had no time limit, or killed only the
direct child on timeout, so a grandchild kept the pipes open. The module
example of `std/io` reads a line of standard input: in CI stdin is empty
and it passed, but from a terminal or an open pipe it waited forever.

## Decision

1. Tests are hermetic. `tests/runner/run_tests.py` and
   `tools/run_std_tests.py` give every process an empty standard input,
   start it in its own process group and, past the time limit (20 s a
   test, 600 s a std module), kill the whole group and report a failure.
   The `std/io` example is `no_run` (spec 12.5). The stage 1 test
   runner spawns through std.os, which keeps stdin; it is unchanged, as
   changing it would change every Ofir program that spawns.
2. Flakes are tracked, not hidden. `run_tests.py` runs a failing test
   once more. If it then passes it is `FLAKY`: it counts as passed, is
   printed with its first failure, is listed in
   `build/logs/flaky-stage<N>[-TARGET].txt` and, in GitHub Actions, is a
   warning on the run and a line of the job summary
   (`tools/ci_flaky_summary.sh`). A flaky test is never skipped or
   quarantined; it is fixed. `--no-retry` fails on the first failure, and
   `--bless` never retries.
3. `make test` is sharded without losing coverage. The Makefile defines
   `TEST_SHARD_1` and `TEST_SHARD_2`, and `test` as both, in the same
   order as before; `test-shard-1` and `test-shard-2` run one half each.
   CI runs each half as a job of its matrix, Linux and macOS with LLVM 17
   and 21, so every check still runs on every platform and LLVM. The
   cross tests and safety checks run in the Linux jobs of shard 1, the
   shorter half. Windows keeps one job that runs `make test`.
4. A new push to a pull request cancels the run of the previous one;
   runs on `main` and release branches always finish. Every job has a
   time limit (40 minutes, Windows 45).
5. A failed job uploads `build/logs/` (the full output of its tests, and
   the flaky lists) and the `.actual` files of mismatched outputs as an
   artifact kept 14 days.
6. CI caches nothing it builds. The compiler builds itself, the runtime
   and its tests in every job, so a cached output could hide a stage 0 or
   stage 1 change; and the toolchain installs take under a minute. A
   cache is added only for downloads keyed by their exact version.

## Consequences

- The slowest job is about half of the old one, at twice the jobs.
- A check added to `make test` must be added to one of the two shards;
  `test` is the two shards, so it cannot be missed by one and run by the
  other.
- A flaky test no longer fails the run, but it shows in the summary and
  its list stays in the artifacts of a failed run; a test that fails
  twice still fails.
- A test that needs input reads it from its fixture, never from the
  terminal.
