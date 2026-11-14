# 0066. Cross compilation

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-51 asks to build a program for a target other than the host
(ADR 0065): the target's triple, sysroot and runtime must reach clang and
the linker, and its executables must run somewhere for `run` and `test`.

- The generated LLVM modules have no triple, so clang's `--target`
  chooses the code it generates and the C ABI.
- The runtime (`libofir_rt.a`) is C and is built per target. `os.name()`
  and `os.arch()` come from it, so they report the target it was built
  for.
- clang finds a Debian cross sysroot (`/usr/aarch64-linux-gnu`) by
  itself; other sysroots need `--sysroot`.

## Decision

1. **`--target=TRIPLE`** on `build`, `run`, `test` and `bench` selects one
   of the triples of spec 13.8; any other is an error. A triple equal to
   the host's is the same as no `--target`.
2. **`@cfg`** (12.4) takes `os` and `arch` from the target triple. The
   single compilation unit, `-lpthread`, the stack size flag and the
   shared library extension follow the target OS, not the host's.
3. **Toolchain.** The compiler passes `--target=TRIPLE` to clang, which
   stays a host program, and `--sysroot=$OFIR_SYSROOT` when that variable
   is set.
4. **Runtime.** The target's runtime is `<dir>/<triple>/libofir_rt.a`,
   where `<dir>` holds the host runtime (`OFIR_RUNTIME`).
   `make runtime TARGET=triple` builds it; a missing one is E0902.
5. **Running.** `run`, `test` and `bench` run a target executable through
   `OFIR_RUNNER`, a command line such as
   `qemu-aarch64 -L /usr/aarch64-linux-gnu` that is given the executable
   and its arguments. Without it they are an error; `--emit` and `build`
   need no runner.
6. **Scope.** Only stage 1 has `--target`; stage 0 builds for the host.
7. **Tests.** `make test-cross TARGET=triple` runs the run tests through
   the runner. CI runs it for `aarch64-linux-gnu` under qemu-user on
   Linux. A test marked `// requires: host` is skipped there: tests with
   C fixtures or exit statuses, a test of a missing program (qemu-user
   starts it and exits 127), and the 100,000-deep tree drop of
   `drops/containers.of`, which needs about 12 MiB of stack on aarch64
   at `-O0`.

## Consequences

- A Linux host builds and tests aarch64 Linux programs; the tier of
  `aarch64-linux-gnu` stays 2, since only the run tests reach it.
- Each target needs its runtime built once, and a sysroot that clang
  finds.
- A target without an emulator can be built, but not run or tested.
