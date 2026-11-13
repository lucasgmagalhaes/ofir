# 0053. The commands are the compiler service interface

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-37 asks for compiler service interfaces for check, build and
metadata: structured diagnostics, a stable exit status, cancellation, and
contracts for editor integration.

Most of the pieces existed but were not promised: `--message-format=json`
writes one diagnostic per line, `--stop-after=check` checks without code
generation, `ofir lsp` speaks LSP, and the statuses were 0, 1 and 2 by
habit. There was no way for a tool to learn which files make up a
program, so a build tool or an editor could not know what to watch.

A library interface (a C API, or an Ofir package to link) was the other
way. It would tie tools to the compiler's internals and to one process,
and the compiler is not yet stable enough for that.

## Decision

1. **The commands are the interface.** Tools run `ofir` and read its
   output; nothing links the compiler.
2. **`ofir check <file.of>`** is `build --stop-after=check` under its own
   name: diagnostics, no output.
3. **`ofir metadata <file.of>`** loads the program and prints one JSON
   object: a `format` version, the compiler version, the root module, and
   each module's name, path and whether it is in `std`. Fields are added
   without raising `format`, never dropped or renamed. It stops after
   loading, so it answers for a program with errors in its bodies.
4. **Results go to standard output, diagnostics to standard error**, in
   the `--message-format` asked for.
5. **The exit status is a contract**: 0 success, 1 errors or failed
   tests, 2 usage errors, 101 an internal compiler error (a panic), and
   under `run` the program's own status.
6. **Cancellation is a signal.** The compiler keeps no state a killed
   build can corrupt: the object cache gains an object only by a rename
   of a whole file. There is no cancellation protocol beyond that.
7. **The language server answers every request, in order.**
   `$/cancelRequest` is ignored, as are other unknown notifications.

## Consequences

- An editor or build tool needs no binding: it runs a process and parses
  JSON. Each call loads the program again; a long-lived service with a
  cache in memory is left for when a profile shows the need.
- The JSON shapes and the statuses cannot change silently:
  `tools/check_service.py` runs in `make test-stage1`.
- A slow request in the language server delays the ones after it, since
  none is cancelled; requests are checks of one program, short so far.
- `metadata` does not list the package dependencies of `ofir.json` on
  their own; their modules are in `modules` with their paths.
