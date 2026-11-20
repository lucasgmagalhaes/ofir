# 0103. The compilers' coverage is measured, uploaded and ratcheted

- Status: Proposed
- Date: 2026-11-20

## Context

ADR 0073 gave Ofir programs coverage: `--coverage` writes gcov counters.
Nobody measured the compilers themselves. A first measurement, with
every suite `make test` runs, gives:

| Code | Lines | Functions | Branches |
|------|------:|----------:|---------:|
| Stage 0 (`bootstrap/src`, C) | 96.3% | 98.9% | 86.1% |
| Stage 1 (`compiler/`, Ofir) | 86.8% | 98.9% | 60.6% |

The gaps are not spread evenly. Stage 0 reaches 92.0% with its own
tests alone; `compare_stages.py` and the fuzzer run the rest, such as
the `--emit=air` printer. Its weakest files are `driver_paths.c`
(22.2%), `main.c` (65.2%), `check_pattern.c` (73.1%) and
`const_eval.c` (81.5%). In stage 1, the AIR interpreter (68.9%), the
package driver (`driver/pkg`, 73% to 88%), `base/json.of` (75.9%),
`const_eval.of` (72.4%) and `check_comptime_for.of` (77%) are the
weakest files. Each uncovered line is a test that is missing, dead code,
or a bug that no test has reached.

The C runtime (`runtime/src`) and `std` as test programs use it are not
measured yet. Both are linked into programs, not into the compilers.

## Amendment: the runtime and std (6.33b)

The run tests and the std tests measure them. The runtime is built by
clang with `--coverage`. A wrapper gives `--coverage` to each command
that links a program, but not to `--emit` and cross builds, whose output
the tests compare. The counters go to `TMPDIR`, where the compiler and
the runner put the programs, and the test runner adds
`OFIR_TEST_LDFLAGS` to the C programs it links against an Ofir library.
gcovr keeps functions of the same name apart
(`--merge-mode-functions=separate`): methods of different types share
one. The first measurement:

| Code | Lines | Functions | Branches |
|------|------:|----------:|---------:|
| Runtime (`runtime/src`, C) | 84.8% | 92.0% | 67.3% |
| `std` | 86.7% | 97.9% | 56.3% |

They are uploaded with the flags `runtime` and `std`.

## Decision

1. **`tools/coverage.py` measures both compilers.** Stage 0 is built by
   gcc with `--coverage`. Stage 1 is built by stage 1 with `--release
   --coverage`: a debug build runs out of stack in deep tests. Each runs
   the suites `make test` runs with it. The stage 1 tool list is read from
   the `test-stage1` recipe of the Makefile, so the two cannot drift.
   gcovr writes a Cobertura report and a JSON summary per stage
   (`make coverage`).
2. **CI uploads the reports to Codecov.** A Linux job with the newest
   LLVM runs `make coverage` on every pull request and on `main`, and
   uploads with the flags `stage0` and `stage1` (`codecov.yml`). A
   failed upload does not fail CI; a failed suite does.
3. **The target is every reachable line.** New and changed lines of a
   pull request are covered: Codecov's patch target is 100%. The project
   total may not fall. A line that no input can reach (an allocation
   failure, an internal error after a check that already failed) is
   excluded with `GCOVR_EXCL_LINE` or `GCOVR_EXCL_START`/`STOP` and a
   comment that says why. An exclusion is reviewed like code.
4. **The floors are a ratchet.** Each directory has a floor in a checked
   file. A pull request that lowers a directory below it fails; one that
   raises it raises the floor. The floors go up until every directory is
   at 100% of its reachable lines.
5. **Uncovered code is triaged, not only tested.** Each uncovered region
   ends as a test, as removed dead code, or as a bug with a regression
   test. Coverage is a map for finding bugs, not a score.
6. **Bug hunting goes beyond the existing suites.** In order: libFuzzer
   on the stage 0 lexer, parser and checker with sanitizers; a generator
   of well-typed programs whose output is compared between stage 0,
   stage 1, the AIR interpreter and the release build; and mutation
   testing of the compilers, where a surviving mutant is a missing test.

## Alternatives

- **Only line coverage.** Branch coverage shows the conditions a test
  never made false, where line coverage says the line ran. It is
  reported from the start; its floors come after the line floors reach
  100%.
- **100% without exclusions.** Defensive code that no input reaches
  would need fault injection for each line or would be deleted. Marked
  exclusions keep the code and make the reason reviewable.
- **Another coverage service.** Codecov reads Cobertura, comments on
  pull requests, shows flags per stage and is free for open source. The
  reports are plain Cobertura, so another service reads them too.

## Consequences

- CI has one more Linux job. It runs in parallel with the test shards.
- The repository needs the `CODECOV_TOKEN` secret and the Codecov app
  on GitHub; without them the reports are still kept as job artifacts.
- Roadmap 6.33 tracks the work: the runtime and `std`, the exclusions
  and ratchet, the climb per area, and the bug-hunting campaigns.
