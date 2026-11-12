# 17. Union types **[planned]**

A union type `A | B` holds a value of one of its member types (ADR 0022).
It is an anonymous tagged enum: each value carries a tag that says which
member it holds.

```ofir
type LoadError = IoError | ParseError

fn load(path: str) Result<Config, LoadError> {
    const text = fs.readToString(path)?  // Result<str, IoError>: IoError widens
    const config = Config.parse(text)?   // Result<Config, ParseError>
    return Result.Ok(config)
}
```

## 17.1 Union types

- A union has at least two member types once normalized (17.2); `A | A` is
  just `A`.
- A member must be a type that can be the payload of an enum variant: not
  a reference, an unsized type, `!` or a key type (E0351).
- A member cannot be a bare type parameter, and two members cannot become
  the same type for some instantiation: `T | int` and
  `List<T> | List<int>` are errors (E0350), `Option<T> | int` is not. The
  members of a union are thus distinct in every instantiation.
- `|` between two key types is a key type (chapter 16); a `|` with a key
  type on one side only is an error (E0351).

## 17.2 Equivalence

Unions are compared after normalization:

- a member that is itself a union is replaced by its members (flattening),
  which includes unions named by an alias;
- repeated members are removed;
- members are sorted by their canonical name (the full path of the type
  with its type arguments).

So `IoError | ParseError`, `ParseError | IoError` and
`LoadError | IoError` are the same type. Unlike structs and enums, a union
has no name of its own: an alias is only a name for it (spec 2.14).

## 17.3 Widening

A value **widens** to a union `U` when its type is a member of `U`, or is a
union whose members are all members of `U`. Widening happens only where an
expected type is known, the coercion points:

- the value of `const` or `let` with a declared type, and of an assignment;
- an argument, for its parameter's type;
- the value of `return` or of the last expression of a body;
- a field of a struct literal or of a variant;
- the error of `?`: on a `Result<T, E>` in a function returning
  `Result<U, F>`, `E` widens to `F` when `F` is a union containing it
  (before the `From` conversion of chapter 6.2 is tried).

Widening moves the value into the union (or copies it, for a Copy type).
It does not reach inside other types: a `List<IoError>` is not a
`List<IoError | ParseError>`, and a `fn() IoError` is not a
`fn() IoError | ParseError`.

A union is never inferred from the values: when the branches of an `if` or
`match` have different types and no expected type is known, it is an error
(E0353); writing the expected type fixes it.

```ofir
const a = if ok { 1 } else { "one" }               // E0353
const b: int | str = if ok { 1 } else { "one" }    // each branch widens
```

## 17.4 Narrowing

A union is inspected with **type test patterns**, `name: Type` (A.7), in
`match` and `if const`:

```ofir
match err {
    e: IoError => eprintln("cannot read: {e.message}"),
    e: ParseError => eprintln("line {e.line}: {e.message}"),
}

if const e: IoError = err {
    retry()
}
```

- The pattern matches when the value holds a member of type `Type`, and
  binds that member. `_` may stand for the name.
- `Type` may be a union of some of the members, written in parentheses:
  `e: (IoError | NetError)` binds a value of that smaller union.
- `Type` must be a member, or a union of members, of the matched type
  (E0352).
- A `match` on a union is exhaustive when its arms cover every member
  (E0500 lists the missing ones), as for an enum.
- The binding takes the member as the matched place is taken: moved out of
  an owned value, borrowed from a borrowed one (chapter 5).

## 17.5 Operations and interfaces

A union has no methods and no fields of its own. It satisfies the built-in
interfaces `Eq`, `Hash`, `Clone`, `Debug` and `Display` when every member
does; the operation dispatches on the tag. Two unions are equal when they
hold the same member and the members are equal.

- `"{u}"` interpolates the member it holds; it is valid when every member
  can be interpolated, and takes no format specifier.
- `{u:?}` shows the debug representation of the member, without a variant
  name: `3`, `"hi"`, `Point { x: 1 }`.
- A union has no order: `<`, `<=`, `>`, `>=` and `Ord` bounds do not apply
  to it (E0313).

## 17.6 Layout

A union is laid out as an enum with one variant per member, in the
canonical order of 17.2, each variant carrying the member as its only
field (chapter 13). Since that order does not depend on how the union is
written, the same union has one layout everywhere.

## 17.7 Errors

| Code | Error |
|------|-------|
| E0350 | the members `A` and `B` of a union can be the same type |
| E0351 | type `T` cannot be a member of a union |
| E0352 | `T` is not a member of the union `U` |
| E0353 | the branches have different types: write the union type expected |
