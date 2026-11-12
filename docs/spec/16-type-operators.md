# 16. Type operators

Type operators build types from other types during compilation, as
TypeScript's `keyof`, indexed access, mapped and conditional types do
(ADR 0021). Their results are ordinary Ofir types; nothing of them exists
at run time. Stage 1 implements the whole chapter; stage 0 has no type
operators.

## 16.1 Key types and `keyof`

A **key type** is a set of field names. `keyof T` is the key type of the
fields of struct `T`, in declaration order; for a tuple type, its indices.

```ofir
struct Point {
    x: float
    y: float
}

type PointKey = keyof Point      // the keys .x and .y
type First = .x                  // a key literal: one key
type Both = .x | .y              // the same keys as keyof Point
```

- Key literals are written `.name` (`.0`, `.1` for tuple indices).
- `A | B` is the union of two key types, `A & B` their intersection and
  `A - B` the keys of `A` not in `B`. A `|` between other types is a
  union type (chapter 17).
- `keyof` applies to structs and tuples only (E0340). Outside the module of
  a struct, its private fields are not keys. In the body of a type alias,
  the module that uses the alias also counts as inside: `Partial<Point>`
  from the prelude has a field for each private field of `Point` in the
  module of `Point`.
- A key type with one key is a **single key**; with none it is the empty
  key type `keyof ()`.

## 16.2 Keys as generic parameters

A type parameter bounded by a key type accepts any key type included in
it:

```ofir
fn get<T, K: keyof T>(value: &T, key: K) &T[K] {
    return &value[key]
}

fn take<T, K: keyof T>(value: move T, key: K) T[K] {
    return value[key]
}

const p = Point{x: 1.0, y: 2.0}
const x = *get(&p, .x)             // K = .x, the result is a float
const y = take(p, .y)
```

- A parameter of a key type has no runtime value: it is erased, and each
  call is instantiated for its key, like any generic call. Its argument must
  be a key literal or a key-typed parameter of the caller (E0343).
- `value[key]` with a single key is the field: `p[.x]` is `p.x`, a place
  that can be assigned when `p` can.
- In a generic body `T[K]` is not known to be `Copy`: moving
  `value[key]` out needs `value` owned (`move T`); through `&T`, borrow it
  (`&value[key]`). Two key parameters may name the same field, so moving
  both `value[j]` and `value[k]` out is a use of a moved value (E0600).

## 16.3 Indexed access types

`T[K]` is the type of the field `K` of `T`: `Point[.x]` is `float`. With a
key type of several keys, all their fields must have the same type
(E0342): `Point[keyof Point]` is `float`.

## 16.4 Mapped types

A mapped type is a struct with one field per key of a key type, in order:

```ofir
type Partial<T> = { [K in keyof T]?: T[K] }
type Pick<T, K: keyof T> = { [P in K]: T[P] }
type Omit<T, K: keyof T> = { [P in keyof T - K]: T[P] }
type Record<K, V> = { [P in K]: V }

const patch = Partial<Point>{x: Option.Some(3.0)}   // y defaults to Option.None
const only: Pick<Point, .x> = Pick<Point, .x>{x: 1.0}
const limits = Record<.min | .max, int>{min: 0, max: 10}
```

- `[K in Keys]: F` gives each key `K` a field of type `F`, where `F` may
  use `K` (`T[K]`).
- `[K in Keys]?: F` makes each field an `F?` (`Option<F>`) with the default
  `Option.None`, so a literal may leave it out.
- `[K in Keys if Cond]: F` keeps the keys for which the conditional type
  `Cond` (16.5) is `true`: `{ [K in keyof T if T[K] is int]: T[K] }` keeps
  the integer fields.
- Each instantiation is one nominal struct named by the alias and its
  arguments: `Partial<Point>` is the same type everywhere, and differs
  from a hand-written struct with the same fields. Its layout is that of a
  struct with those fields (chapter 13).
- A mapped type is declared with `type`; it cannot have methods of its own.

## 16.5 Conditional types

```ofir
type Unwrap<T> = if T is Option<infer U> { U } else { T }
type Required<T> = { [K in keyof T]: Unwrap<T[K]> }
```

- `if A is B { X } else { Y }` is `X` when type `A` matches the type
  pattern `B`, else `Y`. `infer U` in `B` names the matched part, usable in
  `X`.
- `true` and `false` are the conditions of `if` filters: `T[K] is int` is
  `true` for an `int` field.
- Conditions are decided per instantiation, after substitution; an
  unresolved condition (on a type parameter with no bound that decides it)
  is an error at the instantiation (E0344).
- A type alias may name itself only in a branch of a conditional type:
  `type Deep<T> = if T is Option<infer U> { Deep<U> } else { T }` makes
  `Deep<Option<Option<int>>>` an `int`. Each expansion is decided with its
  arguments; another self-reference is E0310, and more than 64 nested
  expansions of aliases are E0346, reported where the recursive alias is
  first named. A recursive alias whose condition is on a type parameter of
  a generic signature is not supported yet (E0901).

## 16.6 Mapping values

The values of a mapped type are built and read field by field with
compile-time loops over keys (chapter 15):

```ofir
fn merge<T>(base: move T, patch: move Partial<T>) T {
    let out = base
    comptime for key in keyof T {
        if const Option.Some(value) = patch[key] {
            out[key] = value
        }
    }
    return out
}
```

`comptime for key in K` is unrolled once per key of `K`; in each copy
`key` is a single key.

## 16.7 The standard aliases

`std.core` (the prelude) defines:

| Alias | Meaning |
|-------|---------|
| `Partial<T>` | every field of `T` optional, defaulting to `Option.None` |
| `Required<T>` | every `Option` field of `T` unwrapped |
| `Pick<T, K: keyof T>` | the fields `K` of `T` |
| `Omit<T, K: keyof T>` | the fields of `T` but `K` |
| `Record<K, V>` | a field of type `V` for each key of `K` |

## 16.8 Errors

| Code | Error |
|------|-------|
| E0340 | `keyof` needs a struct or tuple type |
| E0341 | `T` has no field `.k` |
| E0342 | the fields of these keys have different types |
| E0343 | a key must be known at compile time |
| E0344 | the condition of a conditional type is not decided for this instantiation |
| E0345 | `infer` is only allowed in the pattern of a conditional type |
| E0346 | a recursive type alias expands more than 64 times |
