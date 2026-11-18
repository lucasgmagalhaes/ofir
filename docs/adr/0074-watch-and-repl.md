# 0074. Watch mode re-runs the driver and the REPL replays its session

- Status: Accepted
- Date: 2026-11-18

## Context

OFIR-62 asks for watch mode, scratch files, a REPL and notebooks, with
quick edit, check and run first, and explicit state semantics for
interactive execution.

The compiler is ahead of time: a program is checked, lowered, emitted as
LLVM IR and linked by clang. There is no interpreter for a whole program
(the AIR interpreter of ADR 0035 runs comptime code only) and no way to
add a function to a running process.

A scratch file needs nothing new: `ofir run file.of` builds and runs one
file without a manifest, and its imports resolve from `std` and from the
file's directory.

## Decision

**Watch mode.** `--watch` on `build`, `run`, `test` and `check` runs the
same command again each time a source changes. The driver starts itself
without `--watch`, passes the output on, and polls the modification
times every 200 ms. It watches the `.of` files and `ofir.json` files
under the directory of the input file, or under the current directory
when the command names no file. It skips hidden directories and
directories named `build`, `target` and `node_modules`. Polling, rather
than inotify, FSEvents or ReadDirectoryChangesW, works the same on every
tier 1 host and needs no runtime support. A run that fails does not stop
the watch. The watch ends with the process (Ctrl-C).

**REPL.** `ofir repl` reads entries from standard input. An entry ends
at the end of a line where its brackets are balanced. Each entry is one
of three kinds:

- An item (`fn`, `struct`, `enum`, `interface`, `impl`, `import`,
  `type`, `pub`, or an attribute) goes at the top of the session module.
- A `let` or `const` binding goes into the session's `main`.
- Anything else goes into `main` as an expression, and its value is
  printed with `{:?}`. When that does not compile, for example for an
  assignment or a loop, it goes in as a statement.

The state semantics is replay. Each entry builds and runs the whole
session from the start, with the new entry last. The REPL shows only the
output that follows the output of the session before the entry. An entry
that fails to compile or panics is dropped, and the session stays as it
was. So a binding keeps its value because its initializer runs again.
Side effects outside the program (files, the clock, randomness) happen
again on every entry. A session whose output changes between runs shows
its whole output. `:show` prints the session module, `:reset` empties
it, and `:quit` (or the end of the input) ends the REPL.

**Notebooks** are not part of the toolchain. A notebook front end can
drive `ofir repl` over a pipe, one cell per entry, with the same replay
semantics.

## Consequences

- Watch mode and the REPL use the normal compiler. They report the same
  diagnostics and run the same code as `ofir build` and `ofir run`.
- An entry costs a full build of the session, which is a fraction of a
  second for a small session. The object cache (ADR 0033) keeps the std
  units warm.
- Diagnostics point into the session module in the temporary directory.
  `:show` prints that module with line numbers.
- A program run under watch or the REPL has its output captured and
  shown when it ends, as with `ofir run`. It does not read the REPL's
  standard input.
- `tools/check_watch.py` checks that a watched run reruns on a change and
  a REPL session keeps its bindings, prints values and drops failed
  entries.
