# 0061. Formatting and testing utilities in std.fmt and std.testing

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-46 asks that assertions, fixtures and benchmarks match the documented
exports, and that their interface and closure dependencies be resolved.

- `docs/std/utilities.md` lists a `std.fmt` module, but there is none:
  format specifiers (spec 1.7) are lowered by stage 1 and take a width or a
  precision written in the literal, never one computed at run time.
- `assertEq` and `assertNe` need `Eq` and `Debug`, so stage 0 tests can
  only `assert(a == b)`, which does not print the values.
- A test has no fixtures: no temporary directory, no expected-output file.
- `Bencher.run` prints its result but a caller cannot read it.

## Decision

1. **`std.fmt` exists and formats at run time, in both stages.**
   - `padStart`, `padEnd` and `center` pad to a width counted in Unicode
     scalar values with a given fill; text that is already wide enough is
     returned unchanged.
   - `fixed(x, digits)` writes a float with `digits` (0 to 9) digits after
     the point, rounded half away from zero; outside the range where that
     is exact (`|x| >= 2^53 / 10^digits`), NaN and infinities, it writes
     what interpolation writes.
   - `grouped(n, separator)` groups the digits of an integer by three;
     `radix(n, base)` writes it in base 2 to 36, with a sign.
   - `bytes(n)` writes a size in binary units (`1.5 KiB`) and
     `duration(ns)` a time in the largest unit that keeps the number at
     least 1 (`1.25 s`, `340 ms`), each with two digits at most after the
     point.
   - `Table` aligns rows of cells in columns, numbers to the right when
     asked; it is for CLI output.
   - Interpolation and its specifiers stay in the compiler. `std.fmt` is
     not what they call.
2. **`std.testing` gains asserts that need no interfaces.**
   - `assertStrEq(actual, expected)` compares text and prints the first
     line that differs, quoted, and its number.
   - `assertApprox(actual, expected, tolerance)` compares floats with an
     absolute tolerance; NaN never matches.
   - `assertContains(text, part)`.
   - They are written in Ofir, so a failure reports the location inside
     `std.testing`, not the call; the message carries the values instead.
     A caller-location attribute is left open.
3. **Fixtures are explicit values.**
   - `TempDir.create(prefix)` makes a fresh directory under `TMPDIR`
     (`TEMP` or `TMP` on Windows, else `/tmp`); `path`, `join(name)` and
     `remove(move self)`, which deletes the tree. Stage 0 has no drop
     hooks, so removal is a call; once destructors reach std the type
     will remove itself when dropped.
   - `assertGolden(actual, path)` compares text with a file; with
     `OFIR_UPDATE_GOLDEN` set and not empty, it writes the file instead.
4. **Benchmarks expose their measurement.**
   - `Bencher.iterations()` and `Bencher.nanosPerIter()` return what the
     last `run` measured. `run` takes a closure, so benchmarks stay stage
     1 only, as spec 12.3 says.

## Consequences

- A stage 0 test can print what differs, use a scratch directory and
  compare with golden files.
- `std.fmt` names functions that interpolation does not use; a program
  chooses widths and precisions at run time with them.
- `assertEq` and `assertNe` stay intrinsics of stage 1: they need `Eq`
  and `Debug`.
