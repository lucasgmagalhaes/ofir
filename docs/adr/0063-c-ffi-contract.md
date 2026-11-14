# 0063. The C FFI contract

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-48 asks that the C FFI cover exports, callbacks, buffers, ownership,
errno and threading, that C fixtures prove the ABI, and that variadics,
unions and callback lifetimes be decided explicitly.

- Spec 11.3 lets integers, floats, `bool` and raw pointers cross by value,
  and `@repr(C)` structs behind a pointer. Spec 11.4 exports an
  `extern "C" fn` with a body as a C symbol.
- An Ofir `fn(A) R` value is two pointers, the code and an environment
  (spec 10.2), and its code uses the internal calling convention. C cannot
  call it, so a C library that takes a callback, such as `qsort`, cannot
  be used.
- C functions report failure through `errno`, which Ofir cannot read.
- Nothing says who frees memory that crosses the boundary, or what happens
  when C calls Ofir from a thread Ofir did not start.

## Decision

1. **C function pointers.** `extern "C" fn(A, B) R` is a type: one code
   pointer, 8 bytes, with the C calling convention. It crosses the
   boundary by value. Its parameter and result types follow spec 11.3:
   C scalars and raw pointers (E0804). Parameters have no modes.
2. **Where they come from.** The name of an `extern "C"` function, one
   with a body or one declared in an `extern "C"` block, is a value of a
   C function pointer type when one is expected and the signatures are
   equal. Closures and Ofir functions are not (E0823): they would need an
   environment C cannot pass. A C function pointer is `copy`.
3. **Calling one is unsafe** (E0803): Ofir cannot know that the pointer
   is valid or that the function behind it keeps the C contract.
4. **Callback lifetimes.** A function has static lifetime, so a C library
   may keep a callback as long as it likes. State goes through the
   library's user-data pointer, passed as `*mut u8` and cast back in the
   callback; the caller keeps that state alive while C may use it. A
   null callback is a `*u8` parameter in the declaration; nullable C
   function pointer types wait for a use.
5. **Variadics and unions.** Calling a variadic C function, such as
   `printf`, is not supported: a fixed-arity declaration for the
   arguments used is undefined behavior on some targets, so it is not
   promised either. Write a C wrapper. C unions have no Ofir type; a
   `@repr(C)` struct with a byte array of the union's size and alignment
   stands in, read and written through raw pointers.
6. **Buffers and ownership.** Each side frees what it allocated. Ofir
   memory comes from the Ofir allocator (a pool over `malloc`), so C must
   not `free` it, and Ofir must not drop memory C allocated: it wraps it
   in a raw pointer and calls the C library's free function. A buffer C
   fills is a `List<T>` passed as `asPtr()`, cast to `*mut T`, and its
   length; C keeps no pointer into it after the call returns unless the
   API says so.
7. **errno.** `os.errno()` reads the calling thread's `errno` and
   `os.setErrno(n)` writes it. Ofir code may allocate between a C call
   and the read, and allocation may change `errno`, so a program reads it
   in the statement after the call.
8. **Threads.** An exported function may run on a thread Ofir did not
   start. Its thread-local runtime state starts empty there, as on a new
   task thread. A panic prints its message and ends the process; it never
   unwinds into C.

## Consequences

- C libraries that take callbacks, such as `qsort` or event loops, are
  usable without C glue, and the fixtures in tests/run/ffi check the ABI
  on every CI target.
- Passing `@repr(C)` structs by value (ADR 0051) and variadic calls remain
  [planned]; both need the C ABI's argument classification.
- Only stage 1 has C function pointer types; stage 0 does not need them.
