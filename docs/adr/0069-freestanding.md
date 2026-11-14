# 0069. No freestanding or embedded target in version 1

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-54 asks whether Ofir should run without an operating system:
embedded, freestanding or no-std code. Its evidence bar is concrete:
the hardware, startup code, interrupts, memory-mapped I/O, the linker
script, panics and an allocator budget.

Measured with clang 18 on the compiler's output for a program that
adds two integers and panics on a wrong result:

- The IR compiles to an `aarch64-none-elf` object with
  `-ffreestanding`. Code generation is not the obstacle.
- That object needs six runtime symbols: `ofir_panic_str`,
  `ofir_panic_overflow`, `ofir_panic_div_zero`, `ofir_panic_cast`,
  `ofir_mem_reserve` and `ofir_mem_commit`. It also needs the C `main`
  in `entry.c`, which sets up the arguments and the stack guard before
  calling `ofir_main`.
- The allocator (`std/core/pool.of`) reserves 16 GiB of address space
  up front and commits it by chunk. A microcontroller has kilobytes of
  RAM and no virtual memory.
- Most microcontrollers (Cortex-M, RV32) have 32-bit pointers, outside
  the 64-bit scope of spec 13.1 and ADR 0065. The 64-bit embedded parts
  (Cortex-A, RV64) usually run Linux, which tier 2 already covers.
- The language has no volatile access, interrupt handler attribute or
  linker section attribute, and std has no split between a core that
  needs no system and the rest.
- No board, workload or owner asks for it.

## Decision

1. Version 1 has no freestanding target and no no-std mode. Programs
   need a hosted target of ADR 0065.
2. A later freestanding target starts from a named board and owner, and
   its ADR settles:
   - **Core library.** Which std modules need no system; the prelude
     must not pull in `std.io`.
   - **Runtime surface.** The panic and memory hooks above become the
     contract a board supplies; `entry.c` is replaced by the board's
     startup code.
   - **Allocator.** A fixed region with a stated budget instead of a
     16 GiB reservation, or no heap at all.
   - **Hardware access.** Volatile loads and stores, interrupt handlers
     and sections, each as a spec change with its own codes.
   - **Pointer width.** 32-bit targets, if the board needs them.
3. Revisit when a board and owner exist, or when 32-bit targets are
   supported.

## Consequences

- The runtime may keep relying on the C library, threads, virtual
  memory and signals.
- `ofir build --lib=static` (ADR 0064) remains the way to put Ofir code
  inside a hosted C program.
- The six-symbol surface above is the starting point if the item is
  reopened.
