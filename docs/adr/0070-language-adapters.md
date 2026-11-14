# 0070. Other languages reach Ofir through the C boundary

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-55 asks for adapters to C++, Python, the JVM, .NET, JavaScript and
plugins. Its evidence is to select the consumers, define a versioned C
or component boundary and test the lifecycle across it.

ADR 0063 fixed the C contract and ADR 0064 the libraries and the
generated header. Every runtime on the list can already call a C ABI:

- C++ includes the header; it has `extern "C"` guards.
- Python has `ctypes` in its standard library and `cffi` outside it.
- The JVM has the foreign function API (Java 22) and JNI, .NET has
  P/Invoke, and Node.js has N-API addons and FFI packages.

A second boundary (a component model, generated bindings per language)
would need a description of types richer than C's, its own versioning
and a maintainer for each language.

## Decision

1. **The C ABI is the boundary.** Other languages use a static or shared
   library from `ofir build --lib` (ADR 0064) and its header; Ofir ships
   no per-language binding generator or runtime adapter in version 1.
2. **Selected consumers.** C++ and Python are the tested consumers:
   - `tests/run/ffi/lib_cpp.of` builds a static library and a C++17
     program that includes the header, passes a `@repr(C)` struct and a
     captureless lambda as the callback, and runs on every tier 1 target.
   - `tests/run/ffi/lib_python.of` builds a shared library and loads it
     with `ctypes`: a struct the caller owns, a Python callback, UTF-8
     text as a pointer and a length (Linux and macOS).

   A consumer named `.cpp` is built with `clang++ -std=c++17`; one named
   `.py` runs with the library's path as its argument.
3. **Versioning.** A library states the version of its boundary with an
   export of its own (`stats_version() i32` in the tests), and a
   consumer checks it before other calls. The generated header and the
   C layout of `@repr(C)` types are the compatibility surface: changing
   them is a new version.
4. **Lifecycle.** As in ADRs 0063 and 0064: the consumer owns memory it
   allocates and passes; memory an export returns goes back through an
   export; output is flushed at exit or by `ofir_stdout_flush()`, which a
   consumer calls before it writes to the same stream. A callback object
   (a `ctypes` function) must stay alive while the library may call it.
   Unloading a shared library is not supported: the runtime keeps
   threads and memory for the process.
5. **Not in version 1:** JVM, .NET and JavaScript fixtures, generated
   bindings, a plugin interface with dynamic loading from Ofir, and a
   component model. Each needs a named consumer and owner; until then
   they go through the C ABI as above.

## Consequences

- The C header stays the one surface to keep stable.
- CI exercises C++ on Linux, macOS and Windows and Python on Linux and
  macOS.
- A binding generator or plugin host is a later item with its own ADR.
