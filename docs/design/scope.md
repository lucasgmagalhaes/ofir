# Scope and capacity

Status: **maintained** (ADR 0102, OFIR-86). `tools/check_scope.py` checks
this page against the [roadmap](../roadmap.md) and the
[maintainers](../maintaining.md#maintainers) in `make check-docs`.

The roadmap says what the project could do. This page says what it does
now, who does it, in what order, and in pull requests of what size. It
also lists the alternatives the project decided against, so that a
proposal starts from the decision instead of repeating it.

## Capacity

Each maintainer has at most one row `in progress` and two rows `next`.
Every other open row is `queued`. A row becomes `in progress` only when
another one is done or goes back to `queued`.

A row that is `in progress` or `next` has its pull requests estimated in
[Next pull requests](#next-pull-requests). A `queued` row is estimated
when it becomes `next`, not before: an estimate of far work is a guess.

## Priorities

The priorities are those of the
[language completeness plan](language-completeness.md#priorities-and-interpretation):
**G0** is implementation truth, safety and trustworthy builds, **G1** is
the general-purpose baseline, and **G2** is an optional specialization.
A G2 row needs a workload and an owner. Without them it is not opened,
and its decision goes in [Rejected and deferred](#rejected-and-deferred).

## Open work

Every open leaf row of the roadmap is here once. A dependency is another
row, which must be done before this one starts. The API rows (6.11 to
6.21a) have their records and gates in the
[API ecosystem plan](api-ecosystem.md).

| Row | Priority | Owner | Depends on | State |
|-----|----------|-------|------------|-------|
| 6.24a | G1 | @lucasgmagalhaes | - | queued |
| 6.32b | G1 | @lucasgmagalhaes | - | in progress |
| 6.32c | G1 | @lucasgmagalhaes | 6.32b | queued |
| 6.32d | G1 | @lucasgmagalhaes | 6.32c | queued |
| 6.32e | G1 | @lucasgmagalhaes | 6.32d | queued |
| 6.32f | G1 | @lucasgmagalhaes | - | next |
| 6.32g | G1 | @lucasgmagalhaes | 6.32c | queued |
| 6.32h | G1 | @lucasgmagalhaes | 6.32g | queued |
| 6.33e | G0 | @lucasgmagalhaes | - | next |
| 6.33f | G0 | @lucasgmagalhaes | - | queued |
| 6.33g | G0 | @lucasgmagalhaes | - | queued |
| 6.33k | G0 | @lucasgmagalhaes | - | queued |
| 6.31c | G1 | @lucasgmagalhaes | - | queued |
| 6.11 | G1 | @lucasgmagalhaes | - | queued |
| 6.12 | G1 | @lucasgmagalhaes | 6.11 | queued |
| 6.13 | G1 | @lucasgmagalhaes | 6.11 | queued |
| 6.14 | G1 | @lucasgmagalhaes | 6.12, 6.13 | queued |
| 6.15 | G1 | @lucasgmagalhaes | 6.14 | queued |
| 6.16 | G1 | @lucasgmagalhaes | 6.15 | queued |
| 6.17 | G1 | @lucasgmagalhaes | 6.12 | queued |
| 6.18 | G1 | @lucasgmagalhaes | 6.17 | queued |
| 6.19 | G1 | @lucasgmagalhaes | 6.11 | queued |
| 6.20 | G1 | @lucasgmagalhaes | 6.14 | queued |
| 6.21 | G1 | @lucasgmagalhaes | 6.15, 6.17 | queued |
| 6.21a | G1 | @lucasgmagalhaes | 6.21 | queued |

## Next pull requests

Sizes: **S** is one concern of about 300 changed lines or fewer, **M** is
about 800 or fewer, and **L** is too large: it is split before it is
listed. Each pull request is a few small commits (`CONTRIBUTING.md`).

| Row | Pull request | Size |
|-----|--------------|------|
| 6.32b | Spec and codes: an error type silences the diagnostics it causes | S |
| 6.32b | Stage 1 suppresses cascades and takes `--error-limit=N` | M |
| 6.32b | Stage 0 the same; compile-fail expectations updated | M |
| 6.32f | `ofir check --fmt` warns about the files `ofir fmt` would change, both stages | S |
| 6.33e | Stage 1 `sema/check` diagnostics: compile-fail cases for the uncovered paths | M |
| 6.33e | Stage 1 `syntax`, `sema/core` and `diag`: the rest of the front end | M |

## Rejected and deferred

A decision here is reopened only by a pull request that changes its ADR
or spec section and shows the condition holds.

| Alternative | Outcome | Decided in | Reopen when |
|-------------|---------|------------|-------------|
| Inheritance, exceptions, operator overloading, mandatory async | rejected | [language completeness](language-completeness.md#architectural-boundaries-and-non-goals) | a proposal shows a library cannot express the semantics |
| Unicode identifiers | rejected | [spec 1.1](../spec/01-lexical.md) | never in version 1 |
| Editions | deferred | [ADR 0037](../adr/0037-compatibility-policy.md) | a break must ship after 1.0 |
| Range values and 128-bit integers | deferred | [ADR 0047](../adr/0047-array-operations-ranges-and-128-bit.md) | a use needs them |
| Fixed-array and `@repr(C)` struct globals (the rest of 6.24a) | deferred | [ADR 0042](../adr/0042-atomics-and-thread-locals.md#amendment-pointer-globals-first) | a program needs one; stage 0 first needs fixed arrays |
| Fallible allocation | deferred | [ADR 0048](../adr/0048-allocation-failure-policy.md) | a program must survive running out of memory |
| WebAssembly target | deferred | [ADR 0068](../adr/0068-webassembly.md) | a WASI or browser program and an owner |
| Freestanding and embedded targets | deferred | [ADR 0069](../adr/0069-freestanding.md) | a named board and an owner |
| A GUI toolkit written in Ofir | rejected | [ADR 0099](../adr/0099-gui-graphics-audio-media.md) | never: native toolkits carry accessibility |
| Tensors, dataframes and GPUs in `std` | rejected | [ADR 0100](../adr/0100-scientific-data-computing.md) | a workload misses its goal as a package |
| Mobile targets and a browser SDK | rejected for version 1 | [ADR 0101](../adr/0101-game-mobile-browser-sdks.md) | an owner, a target triple, emulator CI and a debugger |
