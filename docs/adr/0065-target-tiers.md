# 0065. Target support tiers and the host/target distinction

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-50 asks which targets Ofir supports and how well: CPU, ABI,
endianness, minimum OS and toolchain, and CI coverage, keeping the
current 64-bit scope. It also asks to separate the machine the compiler
runs on (the host) from the machine the program runs on (the target),
which cross compilation (OFIR-51) needs.

- The generated LLVM modules have no target triple or data layout: clang
  uses its default target, except on Windows, where the driver passes
  `--target=x86_64-w64-windows-gnu`.
- Spec 13.1 fixes 64-bit pointers. The layout rules, `@repr(C)` and the
  FFI assume a little-endian target whose C ABI is SysV x86-64, AAPCS64
  (Linux or Apple) or Microsoft x64.
- CI (ci.yml) builds and tests every pull request on ubuntu-24.04
  (x86_64) and macos-15 (aarch64) with LLVM 17 and 21, and on windows-2025
  (x86_64) with the MinGW clang. release.yml builds the compiler on the
  same three.
- `@cfg(os == ...)`, `@cfg(arch == ...)`, `os.name()` and `os.arch()`
  describe the host, which is also the target.

## Decision

1. **Tiers.**
   - *Tier 1*: built and tested by CI on every pull request, and shipped
     by a release. A failure blocks the merge.
   - *Tier 2*: the compiler, runtime and std have the code for it
     (`@cfg`, the runtime's `#if`), and it is expected to work, but CI
     does not run it. A bug there is fixed when it is reported.
   - *Unsupported*: anything else. The compiler may build for it, but
     nothing is promised.
2. **Tier 1 targets.**

   | Target | CPU | C ABI | OS (oldest tested) | Toolchain |
   |--------|-----|-------|--------------------|-----------|
   | `x86_64-linux-gnu` | x86-64 | SysV | Ubuntu 24.04, glibc | clang 17 to 21 |
   | `aarch64-apple-darwin` | ARMv8-A (Apple) | AAPCS64 (Apple) | macOS 15 | clang 17 to 21 |
   | `x86_64-w64-windows-gnu` | x86-64 | Microsoft x64 | Windows Server 2025 | MinGW-w64 clang |

3. **Tier 2 targets.** `aarch64-linux-gnu` and `x86_64-apple-darwin`.
4. **Scope.** Every supported target is 64-bit and little-endian, with
   8-byte pointers and `int`. 32-bit and big-endian targets are out of
   scope for version 1. A tier names the oldest OS and toolchain CI runs;
   older ones may work and are unsupported.
5. **Host and target.** The *host* is the machine the compiler runs on;
   the *target* is the machine the program runs on. `@cfg`'s `os` and
   `arch`, `os.name()`, `os.arch()` and the layout rules describe the
   target. Without cross compilation the target is the host; OFIR-51 adds
   a way to choose it, and the tools the compiler runs (clang, the
   archiver) stay host programs.
6. **Raising a target.** A target moves to tier 1 when a CI job builds
   and tests it on every pull request; it moves down when that job is
   removed, in the same pull request that changes this table.

## Consequences

- Users know which platforms block a merge and which are best effort.
- Spec 13.8 holds the table; a change to it changes this ADR.
- Cross compilation (OFIR-51) starts from the host/target split here: it
  must pass a triple, a sysroot and the target's runtime to clang.
