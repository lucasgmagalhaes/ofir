# Strings: `str` and `std.strings`

`str` methods are available everywhere; `std.strings` adds a few free
functions. All byte offsets are `int`; slicing panics unless offsets fall on
UTF-8 character boundaries.

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
| `fn contains(self, p: str) bool` | | ✓ |
| `fn trim(self) &str`, `trimStart`, `trimEnd` | borrow of `s` | ✓ |
| `fn split(self, sep: str) StrSplit` | borrowing iterator of `&str` | ✓ |
| `fn splitWhitespace(self) StrSplit` | | ✓ |
| `fn lines(self) StrSplit` | splits on `\n`, removing a trailing `\r` | ✓ |
| `fn replace(self, from: str, to: str) str` | allocates | ✓ |
| `fn toLower(self) str`, `toUpper()` | ASCII only; allocates | ✓ |
| `fn repeat(self, n: int) str` | allocates | ✓ |
| `fn toCString(self) CString` | null-terminated copy for FFI | |

Iterating a `str` directly (`for b in s`) yields bytes (`u8`); use
`s.chars()` for Unicode scalar values.

## `std.strings`

| Function | Description | S0 |
|----------|-------------|:--:|
| `fn join(parts: [str], sep: str) str` | | ✓ |
| `fn fromUtf8(bytes: move List<u8>) Result<str, Utf8Error>` | validates | ✓ |
| `struct Builder` with `push`, `pushChar`, `finish() str` | efficient incremental construction | ✓ |

The iterators `StrSplit` (of `split`, `splitWhitespace` and `lines`) and
`StrBytes` are in `std/core/split.of`; their items are views of the string
(`cap == 0`), which nothing frees. Stage 0 does not check that a view does
not outlive its string (no borrow checker yet, roadmap 6.x).

