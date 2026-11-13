# Strings: `str` and `std.strings`

`str` methods are available everywhere; `std.strings` adds a few free
functions, and `std.strings.utf8` and `std.strings.ascii` work on bytes.
All byte offsets are `int`; slicing panics unless offsets fall on
UTF-8 character boundaries.

Text has three levels (ADR 0056):

- **Bytes** are `u8`, and every offset into a `str` is a byte offset:
  `len()`, `find`, `slice`.
- **Unicode scalar values** are `u32`: `chars()`, `charIndices()`,
  `charCount()`, `pushChar`.
- **Grapheme clusters** (what a reader sees as one character, such as
  `e` with a combining accent) have no support in std. Segmentation,
  normalization, regular expressions and locale-aware case mapping are
  separate decisions.

| Method | Description | S0 |
|--------|-------------|:--:|
| `fn str.new() str`, `fn str.withCapacity(n: int) str` | | ✓ |
| `fn len(self) int` | length in bytes | ✓ |
| `fn isEmpty(self) bool` | | ✓ |
| `fn push(mut self, t: str)` | append; may allocate | ✓ |
| `fn pushChar(mut self, c: u32)` | append a Unicode scalar value as UTF-8; **panics** if `c` is not one | ✓ |
| `fn toOwned(self) str` | copy a borrowed string into a new owned `str`; allocates | ✓ |
| `fn clone(self) str` | the same as `toOwned` | ✓ |
| `fn clear(mut self)` | | ✓ |
| `fn bytes(self) StrBytes` | iterator over the bytes (`u8`) | ✓ |
| `fn chars(self) StrChars` | iterator over Unicode scalar values (`u32`) | ✓ |
| `fn startsWith(self, p: str) bool`, `endsWith` | | ✓ |
| `fn find(self, p: str) Option<int>` | byte offset | ✓ |
| `fn rfind(self, p: str) Option<int>` | byte offset of the last match | ✓ |
| `fn splitOnce(self, sep: str) Option<(str, str)>` | views before and after the first `sep` | ✓ |
| `fn stripPrefix(self, p: str) Option<str>`, `stripSuffix` | view without the affix, if present | ✓ |
| `fn asBytes(self) &[u8]` | the bytes, borrowed | ✓ |
| `fn isCharBoundary(self, i: int) bool` | whether `i` can bound a slice | ✓ |
| `fn charCount(self) int` | number of scalar values; O(n) | ✓ |
| `fn charIndices(self) StrCharIndices` | iterator of `(offset, scalar)` | ✓ |
| `fn isAscii(self) bool` | | ✓ |
| `fn eqIgnoreAsciiCase(self, other: str) bool` | | ✓ |
| `fn contains(self, p: str) bool` | | ✓ |
| `fn trim(self) &str`, `trimStart`, `trimEnd` | borrow of `s` | ✓ |
| `fn split(self, sep: str) StrSplit` | borrowing iterator of `&str` | ✓ |
| `fn splitWhitespace(self) StrSplit` | | ✓ |
| `fn lines(self) StrSplit` | splits on `\n`, removing a trailing `\r` | ✓ |
| `fn replace(self, from: str, to: str) str` | allocates | ✓ |
| `fn toLower(self) str`, `toUpper()` | ASCII only; allocates | ✓ |
| `fn repeat(self, n: int) str` | allocates | ✓ |
| `fn toCString(self) CString` | null-terminated copy for FFI | |

A `str` is not iterable itself: use `s.bytes()` for bytes (`u8`) and
`s.chars()` for Unicode scalar values.

## `std.strings`

| Function | Description | S0 |
|----------|-------------|:--:|
| `fn join(parts: [str], sep: str) str` | | ✓ |
| `fn fromUtf8(bytes: move List<u8>) Result<str, Utf8Error>` | validates | ✓ |
| `struct Builder` with `new`, `withCapacity`, `push`, `pushChar`, `finish() str` | efficient incremental construction | ✓ |

## `std.strings.utf8`

| Function | Description | S0 |
|----------|-------------|:--:|
| `fn isScalar(c: u32) bool` | `0..=0x10FFFF` outside the surrogates | ✓ |
| `fn charLen(c: u32) int` | bytes that encode `c`; 0 if it is not a scalar | ✓ |
| `fn encode(c: u32, out: mut List<u8>)` | appends the encoding; **panics** if `c` is not a scalar | ✓ |
| `fn decode(bytes: &[u8], at: int) Option<(u32, int)>` | checked scalar and length at `at` | ✓ |
| `fn lossy(bytes: &[u8]) str` | each invalid byte becomes U+FFFD (`REPLACEMENT`) | ✓ |

## `std.strings.ascii`

Functions of one byte (`u8`); bytes above `0x7F` are never in a class and
are never mapped.

| Function | Description | S0 |
|----------|-------------|:--:|
| `isDigit`, `isHexDigit`, `isAlpha`, `isAlnum`, `isUpper`, `isLower` | classes | ✓ |
| `fn isSpace(b: u8) bool` | the whitespace `str.trim` removes | ✓ |
| `fn toLower(b: u8) u8`, `toUpper` | letters only | ✓ |
| `fn digitValue(b: u8, radix: int) Option<int>` | digit of base 2 to 36 | ✓ |

The iterators `StrSplit` (of `split`, `splitWhitespace` and `lines`),
`StrBytes`, `StrChars` and `StrCharIndices` are in `std/core/split.of`; their items are views of the string
(`cap == 0`), which nothing frees. Stage 0 does not check that a view does
not outlive its string (no borrow checker yet, roadmap 6.x).

