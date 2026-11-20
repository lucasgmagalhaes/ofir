# 0099. GUI, graphics, audio and media are packages over the C boundary

- Status: Proposed
- Date: 2026-11-20

## Context

OFIR-83 (roadmap 6.31e) covers GUI and TUI toolkits, graphics, audio
and media. Its evidence is a package-level workload and the event-loop,
accessibility and platform requirements. It is a G2 decision: it needs
a concrete workload and an owner, and it may be rejected.

What exists:

- The terminal: `std.cli` paints ANSI styles and decides color from
  `NO_COLOR`, `CLICOLOR_FORCE`, `TERM` and `io.isTerminal`, and
  `std.fmt` aligns columns. A program could not learn the width of its
  terminal, which every layout that wraps or fills a line needs.
- The C boundary (ADR 0063, 0070): function pointers with user data,
  `@repr(C)` structs, raw pointers into foreign memory and `@link`. Every
  GUI, graphics, audio and media library (SDL, GLFW, GTK, Cocoa through
  C shims, Win32, PortAudio, FFmpeg) is reached through a C ABI.
- No workload or owner has been named for a GUI, a renderer, an audio
  engine or a media pipeline.

These libraries share one shape: the library owns the loop (a window's
messages, an audio device's buffer requests, a decoder's frames), owns
the memory it hands out (pixels, samples), and calls the program back on
its thread. GUI and audio callbacks must run on a fixed thread (the main
thread for Cocoa and Win32 windows; the device's thread for audio) and
must not block or allocate without bound. Accessibility (screen readers,
keyboard navigation, contrast, scaling) belongs to the native toolkit;
a toolkit written in Ofir would have to rebuild it for each platform.

## Decision

1. **No GUI, graphics, audio or media module in `std`** in version 1.
   Each is a package that binds a C library, in the package index
   (ADR 0081, 0097), owned by whoever needs it. A toolkit written in
   Ofir is rejected: it would lose the platform's accessibility.
2. **The event-loop shape is supported through the C boundary.**
   `tests/run/ffi/event_loop.of` is the evidence: a C toolkit owns the
   loop, an 8x8 pixel buffer and its events (`@repr(C)`), and calls an
   `extern "C"` handler with a user-data pointer to the program's state;
   the handler draws into the pixels and stops the loop by returning 1.
   The handler runs on the thread that called the loop, which is the
   main thread when `main` calls it, as Cocoa and Win32 require.
3. **The terminal is the supported interface.** `std.io.terminalSize`
   gives the columns and rows of a standard stream's terminal (`ioctl`
   `TIOCGWINSZ`, `GetConsoleScreenBufferInfo` on Windows), or `None` for
   a file or a pipe. `tools/check_terminal.py`, in `make test-stage1`,
   runs a program on a 100x30 pseudo-terminal.
4. **Deferred until a TUI workload is named:** raw mode and key events,
   the alternate screen, resize notification (`SIGWINCH`) and a screen
   buffer. They need a terminal mode restored on exit and on panic, and
   input that does not block the task that reads it; the workload
   decides both.
5. **The conditions to reopen** a module for one of these domains: a
   named program and owner, the platforms it needs (tier 1 at least),
   the thread its callbacks run on, its accessibility through the
   native toolkit, and a test that runs in CI without a display or an
   audio device.

## Consequences

- A package that binds a GUI or audio library writes its `extern "C"`
  block, its `@repr(C)` structs and an `extern "C"` handler; the C
  boundary needs nothing new for it.
- A handler gets its state through a raw pointer and `unsafe`; a safe
  wrapper is the package's work, and a closure over the state cannot be
  passed as a C function pointer.
- `Send` does not mark a handler that must stay on the main thread; a
  package keeps such calls on the thread that created the window.
- Browser and mobile SDKs stay with OFIR-85 and ADR 0068; data and
  numerical computing with OFIR-84.
