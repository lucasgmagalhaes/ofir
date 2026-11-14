# 0064. Static and shared libraries and C headers

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-49 asks for static and shared libraries, headers and binding
generation, with a C consumer as evidence, and for native dependencies,
symbols and the ownership of runtime allocations to be specified.

- `ofir build` makes an executable: the runtime's `main` (runtime/src/entry.c)
  sets up the stack guard and the arguments, calls the program's `main`
  and flushes standard output.
- ADR 0063 exports `extern "C" fn` functions as C symbols and says who frees
  what crosses the boundary, but a C program cannot link Ofir code, and
  nothing writes the C declarations of the exports.

## Decision

1. **`--lib=static` and `--lib=shared`.** `ofir build --lib=static file.of`
   writes `lib<name>.a`, an archive of one object that holds the whole
   program, so that a linker that loads it for one symbol also has the
   `std` functions the runtime calls back into; `--lib=shared`
   writes `lib<name>.so` (`.dylib` on macOS, `.dll` on Windows), linked with
   the runtime, the `@link` libraries and `-lm`. `<name>` is the input's
   name without `.of`; `-o` names the library file instead. A library has no
   `main`: its roots are its `extern "C"` functions with bodies, and a
   function named `main` is an ordinary function. Stage 1 only.
2. **Code generation.** A shared library's objects are position
   independent (`-fPIC`), and so is the runtime archive, so that it links
   into one. The flag is part of the object cache's key.
3. **The header.** A library build also writes `<name>.h` next to the
   library: an include guard, `<stdint.h>`, `<stdbool.h>` and `<stddef.h>`,
   then a declaration for each `extern "C"` function with a body outside
   `std`, under its C symbol, in module and declaration order. Ofir types
   map to C: `i8`..`i64` to `int8_t`..`int64_t`, `u8`..`u64` and `uint` to
   `uint8_t`..`uint64_t`, `int` to `int64_t`, `f32` and `f64` to `float` and
   `double`, `bool` to `bool`, `*T` to `const T *` and `*mut T` to `T *`
   (`void` for a pointee without a C name), a `@repr(C)` struct to
   `struct Name`, defined before its first use with its fields in order, and
   `extern "C" fn(A) R` to a function pointer declarator. A function
   returning nothing returns `void`.
4. **Native dependencies.** A static library holds only the program's
   code. The header's first comment lists what a C program links after it:
   the runtime (`libofir_rt.a`), each `@link` library in order, `-lm` and
   `-lpthread`. A shared library already carries them.
5. **Symbols.** The C symbols of the header are the library's interface.
   The other generated functions are internal or hidden, so a shared
   library exports only these and the runtime's `ofir_` functions; a C
   program must not define or call those, except `ofir_stdout_flush`,
   which the header declares.
6. **Runtime state.** Nothing initializes a library: its thread-local
   state starts empty on each thread (ADR 0063), `os.args()` is empty and
   no stack-overflow handler is installed, so an overflow is the consumer's
   crash. Standard output is buffered as in a program; it is flushed at
   process exit, and `ofir_stdout_flush()` flushes it before the consumer
   writes to the same stream.
7. **Allocation ownership.** As in ADR 0063: memory an export returns came
   from the Ofir allocator and goes back to the library, through an export
   that frees it; C never calls `free` on it, and Ofir never frees what the
   consumer allocated.
8. **Binding generation from C headers** (reading a `.h` and writing an
   `extern "C"` block) is not part of this decision: it needs a C parser.
   The `extern "C"` declarations are written by hand.

## Consequences

- C, and any language that links C, can call Ofir code; tests/run/ffi has
  a C consumer of a static and of a shared library.
- A shared library also exports the runtime's `ofir_` symbols; a version
  script can hide them later.
- Generating Ofir bindings from C headers remains [planned].
