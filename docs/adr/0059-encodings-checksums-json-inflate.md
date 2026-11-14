# 0059. Encodings, checksums, JSON and inflate in std; other formats outside

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-44 asks for serialization, byte order, encodings, checksums and
compression. It reuses API-13 (JSON), API-15 (base64 and hex) and API-16
(compression), and asks to assess CSV, TOML, YAML, XML and archives on
their own.

None of these exist in std yet. Programs that read a binary header, print
a digest or exchange JSON write their own code for it. Each of these
formats has one specification, so one implementation in std serves every
program.

## Decision

1. **`std.encoding` has hex, base64 and byte order.**
   - `hexEncode` writes lowercase digits. `hexDecode` reads either case and
     fails on an odd length or a byte that is not a digit.
   - `base64Encode` and `base64Decode` use the standard alphabet with `=`
     padding (RFC 4648, section 4). `base64UrlEncode` and
     `base64UrlDecode` use the URL alphabet without padding (section 5).
     Decoding rejects a byte outside the alphabet, wrong padding and
     nonzero unused bits, so every input has at most one meaning.
   - Byte order: `readU16Le`, `readU32Le`, `readU64Le` and their `Be`
     forms read from a slice at an offset; `writeU16Le` and the others
     append to a `List<u8>`. A read past the end panics, as indexing does.
2. **`std.checksum` has CRC-32 and Adler-32.**
   - `crc32` is the IEEE polynomial of zlib, gzip, PNG and ZIP. `adler32`
     is the zlib checksum.
   - Both have an `update` form that takes the running value, so a
     checksum can be computed in pieces.
   - They are not cryptographic. Hashes such as SHA-256 wait for a crypto
     module with its own review.
3. **`std.json` has a value tree, a parser and a writer.**
   - `Value` is `Null`, `Bool`, `Int`, `Float`, `String`, `Array` or
     `Object`. An object keeps its keys in input order.
   - Numbers: a number without a fraction or exponent that fits `i64` is
     an `Int`. Every other number is a `Float`, so precision is lost only
     where `f64` loses it.
   - The parser follows RFC 8259. It validates UTF-8, decodes escapes
     (surrogate pairs included, a lone surrogate is an error), rejects a
     duplicate key, and stops at a nesting depth of 512.
   - An error has a message and the byte offset where it was found.
   - `stringify` writes the shortest form, and `pretty` indents with two
     spaces. Both write keys in the object's order, and a float that is
     not finite is written as `null`, since JSON has no form for it.
   - Typed encoding to and from structs waits for API-14 and derives.
4. **`std.compress` decompresses DEFLATE.**
   - `inflate` reads raw DEFLATE (RFC 1951). `zlibDecompress` and
     `gunzip` read the zlib (RFC 1950) and gzip (RFC 1952) wrappers and
     check their Adler-32 and CRC-32.
   - Every function takes a limit on the output size and fails when it
     would pass it, so a small input cannot exhaust memory.
   - Truncated or malformed input is an error, never a panic.
   - Compression and streaming adapters wait for the `Reader` and
     `Writer` interfaces of API-10.
5. **CSV, TOML, YAML, XML and archives stay outside std.**
   - Each has dialects and options (CSV quoting, YAML tags, XML
     namespaces and entities) that a package can decide better.
   - They can be written in Ofir on top of `std.strings` and
     `std.compress`, and published as packages (ADR 0032).

## Consequences

- Programs can read binary formats, encode bytes as text, check data and
  exchange JSON with std alone.
- The JSON parser has fixed limits on depth, so hostile input fails
  instead of exhausting the stack.
- Without compression, a program can read `.gz` files but has to write
  them with another tool.
