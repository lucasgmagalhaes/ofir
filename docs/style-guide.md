# Ofir style guide

The style `ofir fmt` produces and that all code in this repository follows.
`ofir fmt` (docs/design/toolchain.md) applies the layout rules below; the
naming and documentation rules are for the author.

## Layout

- **Indentation:** 4 spaces, never tabs.
- **Line width:** 100 characters. Break long lines after an operator, a `,`
  or an opening bracket (spec 1.3), and indent the continuation by 4 spaces.
- **Braces:** the opening `{` stays on the line of its construct; `else`
  follows the closing `}` on the same line.
- **One statement per line.**
- **No inline blocks** (`"fmt": {"inlineBlocks": false}`, the default): the
  body of a function and the block of a statement (`if`, `else`, `for`,
  `unsafe`, a `match` arm, the `else` of `const ... else`) always start on
  the line after `{`, and `}` closes on a line of its own, even for a
  single statement:

  ```ofir
  fn name(self) str {
      return "circle"
  }

  if done() {
      break
  }
  ```

  An `if` or `unsafe` used as a value may stay on one line when each of its
  blocks holds a single expression: `const n = if a < b { a } else { b }`,
  `const byte = unsafe { *p }`. `tools/check_format.py` (`make check-format`,
  part of `make test`) enforces the rule on every Ofir source and every
  `ofir` code block of the documentation; `--fix` breaks the lines.
- **Blank lines:** one between top-level items and between methods; none at
  the start or end of a block.
- **Trailing commas** in every multi-line list (arguments, literals,
  parameters); none in single-line lists.
- **Spaces:** around binary operators and `=`; after `,` and `:`; none inside
  brackets, before `(` in calls, or around `.` and `..`.

```ofir
const total = items.len() + extra
const point = Point{x: 1.0, y: 2.0}
for i in 0..n {
    process(list[i], options)
}
```

## Files

Order of a file's contents:

1. the `/** @module ... */` documentation;
2. imports, sorted alphabetically, standard library first, separated from
   project imports by a blank line;
3. public types, then their related private types;
4. public functions, then private functions;
Tests go in the module's test file, `name_test.of` next to `name.of`
(`index_test.of` for a directory's `index.of`), not in the module itself.

## Types

- Order inside a type: fields (or variants), associated functions
  (constructors first, `new` before others), then methods, then `drop`.
- Prefer `fn new(...) T` as the main constructor name; other constructors
  are named after what they take: `fromStr`, `withCapacity`.
- Fields that have a sensible default declare it (`port: int = 8080`)
  instead of requiring it in every literal.
- One-field wrappers use tuple structs: `copy struct NodeId(int)`.
- Prefer generics, a union type or a dedicated interface to `any`;
  keep `any` for values that pass through code that cannot know their
  type (spec 9.8).

## Names

Spec 1.4 defines the conventions (`camelCase`, `PascalCase`,
`SCREAMING_CASE`, lowercase file names). In addition:

- Booleans read as questions: `isEmpty`, `hasErrors`, `canRetry`.
- Functions that may fail return `Result` and are named for what they do,
  not `tryX`; panicking variants say so (`unwrap`, `expect`).
- Conversions: `toX` allocates or copies (`toOwned`, `toList`), `asX`
  borrows (`asRef`), `intoX` consumes (`intoVec2`).
- Avoid abbreviations except well-known ones (`id`, `len`, `src`, `ctx`).

## Control flow

- Handle the failure case first and keep the main path unindented with
  `const ... else` or early `return`.
- Prefer `match` over chains of `if` on the same enum.
- `match` arms are one line when they fit; otherwise use a block.
- Use `??` for defaults instead of `match` on `Option`.

```ofir
fn load(path: str) Result<Config, IoError> {
    const text = fs.readToString(path)?
    const Option.Some(first) = text.lines().first() else {
        return Result.Ok(Config{})
    }
    return parseConfig(first)
}
```

## Comments

- `/** ... */` documents every `pub` item (ADR 0020): one summary
  sentence, then details, then the tags `@typeParam`, `@param`,
  `@returns`, `@panics`, `@safety` and an `@example` where it helps.
  `tools/check_docs.py` checks the tags against the signatures in `std`.
- Ordinary comments explain *why*, not *what*. Write full sentences.
- `// TODO(name): ...` for known follow-ups, with a roadmap task or issue
  number when one exists.

## Size and structure

Small files and small functions are usually easier to read, review and
test, but size is a judgment, not a rule: a file is as long as its subject
needs. `std/core/list.of` holds `List` and all its methods (ADR 0015), and
that is fine at 500 lines.

The one fixed rule is the **line width: 100 characters**, which
`tools/check_sizes.py` enforces in `make test`. The other sizes are
suggestions that `python3 tools/check_sizes.py --advice` lists without
failing, as a prompt for a look in review:

| Suggestion | Around |
|------------|-------:|
| Lines per file | 400 |
| Lines per function | 40 |
| Nesting depth inside a function | 3 |
| Parameters per function (not counting `self`) | 5 |

- **One file, one subject; one function, one nameable task.** If a
  function needs a comment to separate its "parts", each part is usually a
  function.
- Many parameters often mean a missing struct: group related parameters
  into one (`Context`, `Options`).
