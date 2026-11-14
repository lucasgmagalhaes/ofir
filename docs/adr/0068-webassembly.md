# 0068. No WebAssembly target in version 1

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-53 asks whether Ofir should run as WebAssembly, under WASI or in a
browser, and says that a browser host is a separate contract: a Wasm
target does not bring DOM access with it.

Measured with clang 18, `wasm-ld` and node 22 on the compiler's output
for a small program:

- The emitted LLVM IR compiles to a `wasm64-unknown-unknown` object, and
  with the triple overridden to a `wasm32-unknown-wasi` object. Code
  generation is not the obstacle.
- Ofir fixes 64-bit pointers and little-endian targets (spec 13.1,
  ADR 0065). `wasm32`, the only target with a WASI C library
  (wasi-libc) and the one browsers and WASI runtimes run by default,
  has 32-bit pointers; `str`, slices and layouts would change.
- No WASI C library exists for `wasm64`, and the runtime needs one:
  `std.io`, `std.fs` and `std.os` sit on the C library.
- The runtime uses threads (`pthread` in `channel.c`, `sync.c`,
  `task.c`, `io.c`), reserves and commits memory with `mmap` (`pool.c`),
  guards the stack with `sigaltstack` and `SIGSEGV` (`stack.c`) and
  starts processes. WASI preview 1 has none of these, and Wasm threads
  need shared memory that WASI preview 1 does not define.
- No user program, package or owner asks for a Wasm build today.

## Decision

1. Version 1 has no WebAssembly target. `--target=wasm32-*` and
   `wasm64-*` are not tier 1 or tier 2 (ADR 0065); nothing in CI builds
   them.
2. A later Wasm target is two separate contracts, each with its own ADR:
   - **WASI**: a 32-bit layout or a `wasm64` C library, a runtime
     without threads, `mmap` or signals (or WASI threads), and `std.os`
     and `std.fs` limited to what the host grants.
   - **Browser**: a JavaScript host and its imports; DOM and web APIs
     come through declared host functions, never from the target alone.
3. Revisit when one of these holds: a workload with an owner needs it, a
   `wasm64` WASI C library is available, or the language supports
   32-bit targets.

## Consequences

- The 64-bit layout, the runtime's threads and its memory pool stay as
  they are.
- Programs that need the browser keep using another language at that
  boundary, or a C library built for Wasm by other tools.
- OFIR-85 (browser SDKs) depends on this decision being reopened first.
