# 0038. Feature states in one registry; no feature flags before 1.0

- Status: Accepted
- Date: 2026-11-12

## Context

The specification marks designs **[planned]** and questions **[open]**;
the conformance matrix says what each stage implements; stage 1 rejects
what it does not implement yet with E0901; ADRs record decisions,
including what Ofir will not have (macros, `async`/`await`, overloading).
Nothing lists these together, nothing says who owns a feature or how it
moves from one state to the next, and the ADR index only has statuses
that nothing checks. OFIR-07 asks for owners, gates, and documented flags
or rejection decisions.

Considered:

- **Rust**: unstable features behind `#![feature(x)]` on nightly, a
  tracking issue each, stabilized by a team decision (FCP).
- **Zig** and **Go before 1.0**: no flags; the language changes with each
  release, and proposals are accepted or declined.
- **Swift**: `-enable-experimental-feature X` flags next to the evolution
  proposals.

## Decision

- A feature has one of five states: **available** (specified and
  implemented by stage 1, with tests), **partial** (some rules or stages
  missing, classified in the conformance matrix), **planned** (designed in
  the specification, not implemented), **open** (not decided) and
  **rejected** (decided against, with the reason).
- `docs/design/feature-states.md` lists every planned, open and rejected
  feature with its owner, its gate and the decision that settles it.
  Available and partial features are the rows of the conformance matrix.
  `tools/check_features.py` keeps the registry in step with the markers of
  the specification and with the ADR index.
- The **owner** of a feature is the component its work lands in (a
  specification chapter, a compiler stage or a `std` module). The project
  maintainer accepts or rejects ADRs and specification changes.
- The **gate** is what must exist before a feature changes state: a
  planned feature becomes available with its specification, both
  `tests/run` and `tests/compile_fail` cases, and its conformance row; an
  open question becomes planned or rejected only through an ADR.
- There are **no feature flags** before 1.0. ADR 0037 already makes every
  feature unstable, so a flag would protect nothing; an unimplemented
  feature is rejected with E0901 or a dedicated diagnostic, never enabled
  by an option. Flags are reconsidered with the 1.0 promise.
- An ADR is **Proposed** while its pull request is open and **Accepted**
  when it merges; a decision against a proposal is recorded as
  **Rejected**, and a replaced one as **Superseded by NNNN**. The status
  line of each record and the ADR index agree.

## Consequences

- Each change of state is one pull request that edits the registry, the
  specification and, for an open question, an ADR.
- Users have one page that says what Ofir does not have and why.
- Experiments live on branches, not behind options in a release.
