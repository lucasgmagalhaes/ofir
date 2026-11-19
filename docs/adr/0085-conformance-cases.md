# 0085. Conformance cases and feature interactions

- Status: Proposed
- Date: 2026-11-19

## Context

OFIR-72 asks for positive and negative cases mapped to the conformance
matrix (OFIR-01, docs/design/conformance.md) across stages and target
tiers, and for tests of features used together.

What exists: the matrix has one row per section of the specification,
with the state of each stage, the evidence and the gaps;
`tools/check_conformance.py` checks that the rows follow the
specification and that every evidence path exists. The test runner runs
every test with stage 0 and again with stage 1 (`make test-stage1`), and
CI runs both on each tier 1 target (ADR 0065).

What is missing:

- nothing says whether a section is tested by a program that must be
  accepted, one that must be rejected, or both: 36 implemented sections
  list no negative case, 15 no positive case;
- a row can say stage 0 is `full` while every test of it has
  `// requires: stage1`, so stage 0 runs none of them;
- tests mostly exercise one feature; nothing lists which combinations
  are covered (generics with drops, closures with moves, errors with
  loops), and that is where both stages have had bugs.

Rust's test suite is organized by feature, with `ui` tests for errors;
the Go specification tests (`test/`) mix accepted and rejected programs
with `// errorcheck`; C and C++ conformance suites map each test to a
clause of the standard.

## Decision

### Cases

1. **An evidence path is a positive or a negative case by where it
   lives.** Negative: under `tests/compile_fail/` or `tests/panic/`, or a
   file under `tests/borrow/` with an `// error[` annotation. Positive:
   any other test. A directory counts as each kind of case it holds.
   Source paths (`compiler/`, `bootstrap/`, `std/`) are neither.
2. **A section a stage implements (`full` or `partial`) needs both
   kinds of case.** Without one, the row has the gap
   `untested: no positive case` or `untested: no negative case`, followed
   by the reason when there is nothing to reject (a section that only
   names a type, or a layout rule).
3. **A section stage 0 implements needs a case stage 0 runs**: an
   evidence file without `// requires: stage1`, or a directory holding
   one. Otherwise the row has `untested: no stage 0 case`.
4. Target tiers stay as they are: CI runs every case on each tier 1
   target; a case that only runs on some has the `target:` gap and its
   `requires:` line.

### Feature interactions

5. **Interaction cases live in `tests/run/interactions/` and
   `tests/compile_fail/interactions/`.** Each file starts with
   `// sections: N.M N.M ...`, two or more sections of the specification
   that it uses together.
6. **The matrix gets an "Interactions" table**: one row per combination
   (its name, its sections, its cases). The checker requires each
   listed section to exist, each case to declare exactly those sections,
   and each interaction file to be in the table.
7. The first combinations are those where bugs were found: generics and
   destructors, closures and moves, errors and loops, enums and
   patterns with ownership, interfaces and generics, iterators and
   borrows, and tasks with channels and drops.

### Docs

8. The matrix explains the two kinds of case and the table;
   `tests/README.md` describes `// sections:`. No new error code.

## Consequences

- Every implemented section has a program that must compile and one
  that must fail, or a written reason why not; the gaps say what to add.
- Stage 0's `full` claims are backed by tests it runs.
- Interactions are listed, so a new feature can say which combinations
  it adds.
- The matrix grows a column of gaps, and each new section must bring
  both kinds of case.
