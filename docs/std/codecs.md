# `std.encoding`, `std.checksum`, `std.json`, `std.compress`

Data formats (ADR 0059): text encodings of bytes, byte orders, checksums,
JSON and DEFLATE decompression. Every decoder is strict and returns an
error for malformed input instead of panicking.

## `std.encoding`

Decoding rejects a byte outside the alphabet, wrong padding and unused
bits that are not 0, so each text has at most one meaning.

| Item | Description | S0 |
|------|-------------|:--:|
| `struct DecodeError { offset: int }` | the first wrong byte, or the length when the text is too short | ✓ |
| `fn hexEncode(data: &[u8]) str` | lowercase, two digits a byte | ✓ |
| `fn hexDecode(text: str) Result<List<u8>, DecodeError>` | either case | ✓ |
| `fn base64Encode(data: &[u8]) str`, `fn base64Decode(text: str) Result<List<u8>, DecodeError>` | RFC 4648 alphabet, with padding | ✓ |
| `fn base64UrlEncode(data: &[u8]) str`, `fn base64UrlDecode(text: str) Result<List<u8>, DecodeError>` | URL-safe alphabet, no padding | ✓ |
| `fn readU16Le(data: &[u8], at: int) u16`, `readU16Be`, `readU32Le`, `readU32Be`, `readU64Le`, `readU64Be` | **panics** when the bytes are out of range | ✓ |
| `fn writeU16Le(out: mut List<u8>, v: u16)`, `writeU16Be`, `writeU32Le`, `writeU32Be`, `writeU64Le`, `writeU64Be` | appends the bytes | ✓ |

## `std.checksum`

They detect accidental changes; they are not cryptographic. The `Update`
forms continue a value, so data can be checked in pieces.

| Item | Description | S0 |
|------|-------------|:--:|
| `fn crc32(data: &[u8]) u32` | CRC-32 (IEEE), as in zlib, gzip, PNG and ZIP | ✓ |
| `fn crc32Update(crc: u32, data: &[u8]) u32` | start from 0 | ✓ |
| `fn adler32(data: &[u8]) u32` | Adler-32, as in zlib | ✓ |
| `fn adler32Update(adler: u32, data: &[u8]) u32` | start from 1 | ✓ |

## `std.json`

RFC 8259. An object keeps its members in input order. A number without a
fraction or exponent that fits `i64` is an `Int`; any other is a `Float`.
The parser rejects comments, trailing commas, duplicate keys, numbers out
of range and nesting deeper than 512.

| Item | Description | S0 |
|------|-------------|:--:|
| `enum Value` | `Null`, `Bool(bool)`, `Int(i64)`, `Float(f64)`, `String(str)`, `Array(List<Value>)`, `Object(List<Member>)` | ✓ |
| `struct Member { key: str, value: Value }` | a member of an object | ✓ |
| `struct ParseError { message: str, offset: int }` | | ✓ |
| `fn parse(text: str) Result<Value, ParseError>` | | ✓ |
| `fn stringify(v: Value) str` | compact; a float always has a `.` or an exponent | ✓ |
| `fn pretty(v: Value) str` | one member or item a line, two spaces a level | ✓ |

```ofir
import std.json

fn main() {
    match json.parse("\{\"a\": [1, 2.5, null]\}") {
        Result.Ok(v) => println(json.stringify(v)), // {"a":[1,2.5,null]}
        Result.Err(e) => println("error at {e.offset}: {e.message}")
    }
}
```

## `std.compress`

Decompression of DEFLATE (RFC 1951), zlib (RFC 1950) and gzip (RFC 1952).
Each function takes `limit`, the largest output allowed, and fails when
the output would pass it. Compression waits for the `Reader` and `Writer`
interfaces of API-10.

| Item | Description | S0 |
|------|-------------|:--:|
| `struct DecompressError { message: str, offset: int }` | | ✓ |
| `fn inflate(data: &[u8], limit: int) Result<List<u8>, DecompressError>` | raw DEFLATE | ✓ |
| `fn zlibDecompress(data: &[u8], limit: int) Result<List<u8>, DecompressError>` | checks the Adler-32; no preset dictionary | ✓ |
| `fn gunzip(data: &[u8], limit: int) Result<List<u8>, DecompressError>` | checks each member's CRC-32 and size | ✓ |
