# 0090. Representative applications

- Status: Proposed
- Date: 2026-11-19

## Context

OFIR-76 asks for real applications that keep building and passing their
tests across releases: the compiler, a CLI and filesystem utility, a C
consumer, a concurrent program and an API service.

The test suite has many small programs, each written to exercise one
feature. That catches a feature that breaks, but not a combination that
real programs reach and tests do not. Stage 1 is the only large program,
and it is written in the bootstrap subset, so it uses no closures, no
tasks and little of std. The first draft of the concurrent app below
found such a bug at once: `a ?? b`, `a && b` and `a || b` emitted
invalid LLVM IR when `b` diverged.

## Decision

1. `apps/` holds one directory per application, each a normal Ofir
   program with its own test data, written as a user would write it:
   - `seek`, a CLI and filesystem utility: it searches the files of a
     directory tree for a text, with std.cli, std.fs and std.path;
   - `textstats`, a C consumer: an Ofir static library
     (`--lib=static`, ADR 0064) with a `@repr(C)` struct, linked by a C
     program through the generated header;
   - `wordfreq`, a concurrent program: one std.task task per file,
     counts sent through a std.sync channel and a shared `AtomicInt`,
     with output independent of the order the tasks end in.
2. Each app has an `app.json` that names its root, its library kind and
   consumer, and its cases: arguments, expected standard output in
   `expected/<case>.out` and exit status. Usage errors and missing
   files are cases too.
3. `tools/check_apps.py` (`make check-apps`, part of `make test`)
   builds every app with stage 1 in dev and with `--release`, and runs
   every case against both builds, on every CI platform.
4. The compiler itself stays the largest application: `make verify`
   (stage 2 and stage 3 emit the same `.ll`) and the self-compile
   budgets (ADR 0089) already check it.
5. The API service is the reference service of the API ecosystem plan
   (API-49). It needs std.net and an HTTP server, which std does not
   have yet; it joins `apps/` when they land.
6. A change that breaks an app is fixed in the compiler or std, not by
   changing the app, unless the app relied on behaviour the spec does not
   promise; then the app changes in the same pull request with the reason.

## Consequences

- A release cannot ship with a broken CLI, C consumer or concurrent
  program: the same check gates every pull request.
- The apps need stage 1 (closures, `--lib`), so stage 0 does not build
  them.
- Each app adds a few seconds to `make test`: two builds and its cases.
- The apps grow with std: a new module that real programs would use is
  a reason to use it in an app.
