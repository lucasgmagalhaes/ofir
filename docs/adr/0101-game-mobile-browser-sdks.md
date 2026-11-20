# 0101. Games run on the desktop targets; mobile and browser SDKs wait

- Status: Proposed
- Date: 2026-11-20

## Context

OFIR-85 (roadmap 6.31h) covers games, mobile and browser application
SDKs. Its evidence is bindings, deployment, cross-debugging, target
support and named maintainers. It is a G2 decision: it needs a workload
and an owner, and it may be rejected.

What exists:

- Desktop targets: tier 1 is `x86_64-linux-gnu`, `aarch64-apple-darwin`
  and `x86_64-w64-windows-gnu`; tier 2 is `aarch64-linux-gnu` and
  `x86_64-apple-darwin` (ADR 0065). `--target` names only these, and any
  other triple is an error (spec 13.8, ADR 0066).
- The C boundary: an Ofir static or shared library is consumed from C,
  C++ and Python (ADR 0070), and a C toolkit can own the event loop and
  call an Ofir handler (ADR 0099, `tests/run/ffi/event_loop.of`).
- Float loops within 10% of C (ADR 0100), a monotonic clock
  (`std.time.now`) and a seeded generator (`std.random`).
- No WebAssembly target (ADR 0068), and no workload or owner for a game
  engine, a mobile app or a browser app.

A game is a program over a platform library (SDL, GLFW, raylib) that
owns the window, the input and the audio device, with a frame loop on
the main thread. A mobile app is a host process written in Kotlin or
Swift that loads native code through the C ABI (JNI on Android, a
static library in an Xcode target on iOS), on a target with its own
libc (Bionic) or SDK, packaged, signed and debugged by the platform's
tools. A browser app needs a Wasm target and a JavaScript host.

## Decision

1. **Games are programs on the desktop targets.** They bind a platform
   library as a package over the C boundary, in the shape of ADR 0099;
   no game module or engine goes in `std`.
2. **No mobile target in version 1.** `aarch64-linux-android` and
   `aarch64-apple-ios` are unsupported: `--target` rejects them. They
   are 64-bit and little-endian, so the layout rules fit; what is
   missing is a runtime built against the NDK or the iOS SDK, a CI job
   that runs the tests on an emulator or a simulator, and an owner.
3. **The mobile boundary, when it comes, is the C ABI.** Ofir code is a
   static or shared library (ADR 0070) that the app's Kotlin or Swift
   code calls; the build, packaging and signing of the app stay with
   Gradle and Xcode. An Ofir SDK that wraps the platform's UI is
   rejected, for the accessibility reason of ADR 0099.
4. **No browser SDK.** It depends on reopening ADR 0068 for a browser
   Wasm target first.
5. **The conditions to reopen** a mobile or browser target: a named
   program and owner, the target triple and its oldest OS, a runtime
   built for it in CI, the tests run on an emulator, a simulator or a
   Wasm host on every pull request (tier 1 under ADR 0065), and a
   debugger that attaches to the program on it.

## Consequences

- A game that ships on Linux, macOS and Windows needs nothing new from
  the language; its platform binding is a package.
- A mobile app cannot contain Ofir code until decision 5 holds; the
  shape of that work is a new row of spec 13.8.
- The web keeps using another language at its boundary, as ADR 0068
  says.
