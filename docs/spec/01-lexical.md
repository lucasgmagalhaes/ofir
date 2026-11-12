# 1. Lexical structure

## 1.1 Source files

Source files are UTF-8 encoded text with the extension `.of`.

- **Encoding.** The whole file, comments included, must be well-formed
  UTF-8: an invalid sequence, an overlong form, a surrogate or a value
  above U+10FFFF is an error (E0008). So every string literal is valid
  UTF-8. There is no byte order mark: a leading U+FEFF is E0001.
- **Line ends.** A line ends at `\n`. `\r` is whitespace, so `\r\n` ends
  a line as `\n` does; a `\r` alone does not.
- **ASCII tokens.** Names, keywords, numbers, operators and whitespace are
  ASCII. Any other character outside a comment or a literal is E0001,
  including Unicode spaces such as U+00A0.
- **Literals and comments** may hold any character. A literal keeps the
  bytes as written: there is no Unicode normalization, so `"é"` written
  as U+00E9 and as `e` followed by U+0301 are different strings.
- **Bidirectional controls** (U+202A to U+202E, U+2066 to U+2069) change
  how text is displayed and can make code read differently from how it
  runs. They are an error anywhere in a file (E0009); a string that needs
  one writes it as an escape, `"\u{202E}"`.
- **Diagnostic columns.** A diagnostic gives a line and a column, both
  from 1. The column counts bytes from the start of the line: a tab, and
  each byte of a multi-byte character, is one column. The text and JSON
  formats (`--message-format`) use these columns; the language server
  converts them to UTF-16 code units, as the LSP requires.

Identifiers are ASCII (1.4), so they need no normalization, and two
different names cannot look the same through Unicode confusables. Unicode
identifiers are not planned; if they are added, an ADR decides their
normalization and confusable checks first.

## 1.2 Comments

```ofir
// line comment
/* block comment */
```

Block comments do not nest.

## 1.3 Statement termination

Ofir has no semicolons. The rule is:

> A newline ends a statement **wherever the statement could end**.
> Anywhere else, the newline is ignored and the statement continues on the
> next line.

The lexer emits a newline token at the end of each line; the parser treats
it as a terminator only where the grammar allows the current statement to
end, and skips it otherwise. (Unlike Go's token-based rule, this works with
`<...>` generics: `let buffer: List<u8>` at the end of a line is complete,
while `a >` at the end of a line clearly continues.)

```ofir
let buffer: List<u8>          // complete: the newline ends the statement
const ok = width > 0 &&
    height > 0                // `&&` needs an operand: the newline is ignored
const total = price
    + tax                     // error: `price` was complete, `+ tax` is a new statement
```

To split a long expression, end the line with the operator, a `,`, or an
opening bracket.

Newlines directly inside `(...)` and `[...]` are always ignored, so long
argument lists and array literals can span several lines:

```ofir
const total = add(
    first,
    second,
)
```

"Directly" means that the innermost open bracket is `(` or `[`. Inside a
`{ }` block the newline rule applies again, even when the block itself is
inside parentheses, so a closure passed as an argument has normal
statements:

```ofir
task.run(move () => {
    const x = compute()       // the newline ends this statement
    send(x)
})
```

A consequence: an opening `{` must be on the same line as the construct it
belongs to, and `else` must follow `}` on the same line.

## 1.4 Identifiers

```ebnf
identifier = letter { letter | digit } .
letter     = "a"…"z" | "A"…"Z" | "_" .
digit      = "0"…"9" .
```

### Naming conventions

| Kind | Convention | Example |
|------|------------|---------|
| functions, methods, variables, parameters, fields | `camelCase` | `readToString`, `lineCount` |
| types, interfaces, enum variants, type parameters | `PascalCase` | `TokenKind`, `IoError`, `.NotFound`, `T` |
| constants | `SCREAMING_CASE` | `MAX_USERS` |
| modules and file names | lowercase `snake_case` | `std.collections`, `word_count.of` |

Acronyms are written as words: `parseUrl`, `HttpClient`, `IoError` (not
`parseURL`, `HTTPClient`). Module and file names stay lowercase so that they
behave the same on case-insensitive file systems.

The conventions are checked by a warning (chapter 12.6), not by the grammar.
Names declared in `extern "C"` blocks are exempt, since they must match C.

## 1.5 Keywords

Reserved keywords cannot be used as identifiers:

```
as        break     const     continue  copy      dyn       else
enum      extern    false     fn        for       if        import
in        interface let       match     mut       pub       return
self      Self      struct    true      unsafe
```

Contextual keywords are keywords only in one position and remain valid
identifiers everywhere else, because they are common variable and field
names:

| Word | Keyword only when |
|------|-------------------|
| `type` | it starts an item and is followed by an identifier (`type Id = u32`) |
| `move` | it is followed by `fn`, or starts a parameter mode (`x: move T`) |
| `ref` | it is followed by `struct` (`ref struct Iter { }`) |
| `where` | it follows a function signature |
| `comptime` | it starts a declaration, block, `for`, `if` or expression (chapter 15) |

```ofir
const type = token.type      // fine: `type` is an ordinary identifier here
```

## 1.6 Operators and punctuation

```
+    -    *    /    %    &    |    ^    <<   >>   !
+=   -=   *=   /=   %=   &=   |=   ^=   <<=  >>=
==   !=   <    <=   >    >=   &&   ||
=    .    ..   ..=  ,    :    =>   ?    ??   @
(    )    [    ]    {    }    ;
```

The lexer never combines `>` with a following character: `>>`, `>=` and
`>>=` are recognized by the parser from adjacent `>` tokens with no space
between them. This lets `List<List<int>>` close two type argument lists.

`;` is used only inside array types and repeat literals (`[u8; 16]`,
`[0; 16]`); it never terminates statements.

## 1.7 Literals

### Integers

```ofir
42
1_000_000
0xff
0o755
0b1010_0101
```

An integer literal has no fixed type; it takes the type required by its
context and defaults to `int`. A literal that does not fit its type is a
compile error.

### Floats

```ofir
3.14
1e9
2.5e-3
```

A float literal defaults to `float` (64 bits). An integer literal may be used
where a float is expected (`const x: float = 1`).

### Strings

```ofir
"hello\n"
"name = {user.name}, age = {user.age + 1}"
```

Escape sequences: `\n`, `\t`, `\r`, `\0`, `\\`, `\"`, `\{`, `\}`,
`\xHH` (an ASCII character, `\x00` to `\x7F`) and `\u{HHHHHH}` (a Unicode
scalar value). Escapes cannot produce invalid UTF-8, so every `str` is valid
UTF-8. Arbitrary bytes belong in byte strings, `b"\xff\x00"`, of type
`&[u8]`.

**Interpolation:** `{expr}` inside a string literal evaluates `expr` and
inserts its textual form. The type of `expr` must implement string
conversion (all primitive types do). A literal brace is written `\{` or `\}`.

**Format specifiers:** `{expr:spec}` controls formatting:

| Spec | Meaning | Example |
|------|---------|---------|
| `?` | debug representation (`Debug`) | `{list:?}` → `[1, 2, 3]` |
| `.N` | N digits after the decimal point | `{pi:.2}` → `3.14` |
| `N` | minimum width, right aligned | `{n:5}` → `   42` |
| `<N` | minimum width, left aligned | `{s:<8}` |
| `0N` | zero padded | `{n:04}` → `0042` |
| `x` / `X` / `b` | hexadecimal / binary | `{n:x}` → `ff` |

A specifier is `?` or `[<][0][width][.N][x|X|b]`, in that order:
`{pi:8.3}` → `   3.142`, `{n:08b}` → `00101010`. The specifier starts at
the first `:` that is not inside a bracket of the expression. A precision
applies only to floats, a radix only to integers, and zero padding only to
numbers. A negative integer in hexadecimal or binary shows the bits of its
type (`{-1 as i8:x}` → `ff`), and the width counts Unicode scalar values. A
malformed specifier, or one that does not apply to the type, is an error
(E0357).

`?` shows the value as written in code (chapter 9.9): a string quoted,
with escapes. The type must satisfy `Debug` (E0314).

Format specifiers are implemented by stage 1 only.

### Raw strings

Raw strings start with `r"` (single line) or `"""` (multi-line). They have no
escapes and no interpolation.

A multi-line string's text starts on the line after the opening `"""` (only
blanks may follow it on its own line) and ends with the line before the
closing `"""`. The indentation of the closing `"""` is removed from every
line, and every line, the last included, ends with `\n`. A blank line may
be indented less; any other line indented less than the closing `"""` is an
error (E0007). A `\r` before a line break is dropped.

```ofir
const pattern = r"C:\path\{name}"
const text = """
    first line
    second line
    """                        // "first line\nsecond line\n"
```

Raw, multi-line and byte strings are implemented by stage 1 only.

### Characters and bytes

There is no `char` type. A byte literal `b'a'` has type `u8`; a byte string
literal `b"GET \x00"` has type `&[u8]` and accepts any `\xHH` escape.
Unicode scalar values are `u32` (see `str.chars()` in the standard library).

## 1.8 Documentation comments

`/** ... */` documents the next declaration; a `/** ... */` comment whose
text starts with `@module` documents the file's module (chapter 12.5, ADR
0020). The leading `*` of each line is not part of the text. `/***` and
`/**/` are ordinary block comments.
