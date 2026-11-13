# 0056. Text as UTF-8 bytes, scalars as `u32`, graphemes outside std

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-41 asks for bytes and text, UTF-8, builders, search and Unicode
operations, with an explicit distinction between bytes, scalar values and
graphemes. Today:

- `str` is UTF-8, and `s[i]` and `len()` work in bytes.
- `chars()` yields `u32` scalar values.
- `std.strings` has `join`, `fromUtf8` and `Builder`.

Some things are missing:

- a byte view of a string;
- character offsets;
- search from the end;
- splitting once;
- checked prefix removal;
- UTF-8 encoding and decoding for byte buffers;
- ASCII classification.

Normalization, regular expressions, locale-aware case mapping and grapheme
clusters each need Unicode tables or a design of their own.

## Decision

1. **Three levels, three names.**
   - A byte is `u8`, and a byte offset is `int`.
   - A Unicode scalar value is `u32`; there is no separate `char` type.
   - A grapheme cluster has no type in std.

   Every `str` offset is a byte offset. A method that counts characters
   says so in its name, as `charCount` does.
2. **`str` gains:**
   - `asBytes`, a `&[u8]` view;
   - `isCharBoundary`;
   - `charCount`;
   - `charIndices`, which yields `(offset, scalar)`;
   - `rfind`;
   - `splitOnce`;
   - `stripPrefix` and `stripSuffix`, which return `Option<str>`;
   - `isAscii`;
   - `eqIgnoreAsciiCase`.

   Every view these return borrows the string, as `slice` does.
3. **`std.strings.utf8`** works on byte buffers:
   - `isScalar`;
   - `charLen`;
   - `encode`, which appends to a `List<u8>`;
   - `decode`, which reads the scalar at an offset of a `&[u8]` with its
     length;
   - `lossy`, which turns bytes into a `str` and replaces each invalid
     sequence with U+FFFD.

   `fromUtf8` stays in `std.strings`.
4. **`std.strings.ascii`** classifies and maps single bytes:
   - `isDigit`, `isHexDigit`, `isAlpha`, `isAlnum`, `isSpace`, `isUpper`
     and `isLower`;
   - `toLower` and `toUpper`;
   - `digitValue`.

   Case mapping and classification beyond ASCII wait for the Unicode table
   decision.
5. `Builder.withCapacity` reserves the final size up front.
6. Grapheme segmentation, normalization (NFC/NFD), regular expressions and
   locale-aware operations are separate decisions. They will be packages
   with their own data tables, and std does not ship partial versions of
   them.

## Consequences

- Code that slices text has the tools to find valid boundaries
  (`isCharBoundary`, `charIndices`). It no longer needs to rely on the
  panic from `slice`.
- A byte parser reads `s.asBytes()` without copying.
- `toLower` and `toUpper` stay ASCII only, and the docs say so.
- Text shown to people (cursor movement, truncation for display) still
  needs a grapheme package.
