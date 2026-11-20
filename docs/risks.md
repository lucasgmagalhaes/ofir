# Risk register

Risks that could delay or derail the project, with how they are mitigated.
Reviewed at every milestone.

| # | Risk | Impact | Likelihood | Mitigation |
|---|------|:------:|:----------:|------------|
| R1 | Second-class references are too restrictive to write the compiler and `std` comfortably | high | medium | Paper prototype of the stage 1 lexer before stage 0 (roadmap 0.8); `ref struct` and returning borrows from one parameter (spec 5.6); ADR 0002 allows relaxing later |
| R2 | The borrow checker takes much longer than planned | high | high | Borrows are local to one function (no lifetimes); tasks split into moves, loans and escape rules; required for M4 so problems surface early |
| R3 | Stage 0 in C grows too large (estimate: 15–25k lines) | medium | high | Strict bootstrap subset; arenas instead of manual memory management; no diagnostics polish in stage 0 |
| R4 | Textual LLVM IR changes between versions | medium | medium | Supported version range tested in CI; version-dependent syntax in one table (ADR 0001) |
| R5 | Checked arithmetic costs more than expected in hot loops | medium | low | Benchmarks against C from Phase 5; explicit wrapping methods; AIR pass removing provably redundant checks |
| R6 | Monomorphization causes slow builds and large binaries | medium | medium | `dyn` for cold code; measure compile time per milestone; share instances across modules |
| R7 | The newline rule surprises users in edge cases | low | medium | Rule defined on the grammar (spec 1.3); formatter output never hits the edge cases; clear error message suggesting where to break the line |
| R8 | Single maintainer: loss of momentum | high | medium | Small tasks with clear "done when" criteria; documentation complete before code; CI keeps every step working; the accounts, review rules and succession of [maintaining.md](maintaining.md) (ADR 0096) |
| R9 | Name clash: other projects are called "Ofir" (e.g. DataStax Ofir DB) and the repository is still named `pena` | low | high | Check package registries and trademarks before 0.1; rename the repository before the first public release |
| R10 | No concurrency in early releases makes Ofir less attractive than Go | medium | medium | Concurrency (spec chapter 14) is required for 1.0; the design is fixed now so earlier features do not block it |
