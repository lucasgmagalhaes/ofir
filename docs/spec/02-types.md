# 2. Types

Ofir is statically typed. Every expression has a type known at compile
time. Local variable types are inferred; function signatures and struct
fields are always annotated.

## 2.1 Primitive types

| Type | Description |
|------|-------------|
| `i8`, `i16`, `i32`, `i64` | signed integers |
| `u8`, `u16`, `u32`, `u64` | unsigned integers |
| `int` | alias for `i64` |
| `uint` | alias for `u64` (bit manipulation, hashing, C interop) |
| `f32`, `f64` | IEEE-754 floats |
| `float` | alias for `f64` |
| `bool` | `true` or `false` |

**Lengths and indices are `int`**, as in Go: `len()` returns `int` and
indexing takes an `int`. A negative index is simply out of range and panics.
This avoids conversions between signed and unsigned values in everyday code.

There are no implicit numeric conversions. Conversions are explicit with
`as`:

```ofir
const a: i32 = 10
const b = a as i64
const c = 3.9 as int      // 3 (truncates toward zero)
```

`as` between integer types panics if the value does not fit the target
type. Float-to-integer `as` truncates toward zero and panics on NaN or
when the truncated value does not fit, so `-128.9 as i8` is `-128` and
`-129.0 as i8` panics.
Integer-to-float `as` rounds to the nearest representable value.

When a lossy conversion is intended, use the explicit conversion methods,
which take the target type as a type argument:

| Method | Behavior | `300 as u8` equivalent |
|--------|----------|------------------------|
| `x.wrappingCast<T>()` | keeps the low bits (two's complement) | `44` |
| `x.saturatingCast<T>()` | clamps to the target range | `255` |
| `x.checkedCast<T>()` | returns `Option<T>` | `Option.None` |

### Arithmetic

Integer arithmetic is **checked**: overflow, division by zero and shifts
by the bit width or more panic. The cost is a single branch that is
almost never taken; LLVM's overflow intrinsics make it close to free.
Explicit wrapping, saturating and checked arithmetic is available through
methods (`a.wrappingAdd(b)`, `a.saturatingSub(b)`, `a.checkedMul(b)`, ...).

- `/` truncates toward zero; `%` has the sign of the dividend
  (`-7 / 2 == -3`, `-7 % 2 == -1`). `a.divFloor(b)` and `a.remEuclid(b)`
  provide the other conventions.
- `>>` is arithmetic (sign-extending) on signed types and logical on
  unsigned types.

### Floats

Floats follow IEEE-754: division by zero yields an infinity, invalid
operations yield NaN, nothing panics.

- `==` and `<` use IEEE semantics: `NaN != NaN`, every comparison with a
  NaN but `!=` is false, and `-0.0 == 0.0` while `-0.0 < 0.0` is false.
  The two zeros still differ: `1.0 / -0.0` is `-inf` and `-0.0` prints
  as `-0.0`.
- Floats satisfy the `Eq` and `Ord` bounds with these semantics, which
  are not an equivalence or a total order when a NaN takes part (chapter
  9.9). `list.sort()` on floats still sorts every list without a NaN and
  never fails on one: a list with a NaN ends as a permutation of its
  elements, in an unspecified order. A total order that places NaN
  (`totalCmp`) is planned.
- Floats do not satisfy `Hash`: they cannot be `Map` keys, and a struct
  or enum with a float field derives none of `Eq`, `Ord` and `Hash`.
- `a.min(b)` and `a.max(b)` return the other operand when one is NaN;
  of `-0.0` and `0.0` they may return either.
- Textual form (interpolation, `Display`): the **shortest** decimal that
  converts back to the same value, always with a `.` or an exponent:
  `1.0`, `0.1`, `1e+21`, `-0.0`. Special values print as `NaN`, `inf` and
  `-inf`. Exponent notation is used when the exponent is below -4 or at
  least 21.

## 2.2 The unit type

`()` is the type with a single value, also written `()`. Functions without
a return type return `()`.

## 2.3 Strings

`str` is an owned, growable, UTF-8 encoded byte string. String literals have
type `str` but are stored statically and never allocate unless modified.
Passing a literal to a borrowed `str` parameter borrows the static data:

```ofir
fn greet(name: str) {
    println("hello, {name}")
}

greet("Ana")            // no allocation
```

```ofir
let s = "hello"
s.push(", world")
println("{s.len()}")    // 12 (bytes)
const b: u8 = s[0]      // indexing yields bytes
```

## 2.4 Structs

```ofir
struct User {
    name: str
    age: int
}
```

Structs are value types: they are stored inline (on the stack or inside
another value), never boxed implicitly. See chapter 3 for declarations.

## 2.5 Enums

Enums are tagged unions (sum types). Each variant may carry data.

```ofir
enum Shape {
    Circle(radius: float)
    Rect(width: float, height: float)
    Empty
}
```

A value of an enum type is exactly one of its variants. Enums are inspected
with `match` (chapter 4), which must be exhaustive. A union type `A | B`
(chapter 17) is an anonymous enum whose variants are its member types.

## 2.6 Built-in generic types

The standard library provides these generic types, available everywhere
through the prelude. User-defined generics are described in chapter 8.

| Type | Description |
|------|-------------|
| `List<T>` | owned, growable, contiguous array |
| `Map<K, V>` | owned hash map that iterates in insertion order |
| `Option<T>` | `Option.Some(T)` or `Option.None` |
| `Result<T, E>` | `Result.Ok(T)` or `Result.Err(E)` |
| `Box<T>` | owned heap allocation (needed for recursive types) |

```ofir
const xs: List<int> = [1, 2, 3]
let ages = Map<str, int>.new()
const first: Option<&int> = xs.first()   // borrows xs (chapter 5.6)
```

`T?` is shorthand for `Option<T>`: `fn find(xs: [int], x: int) int?`.

**Map ordering and hashing.** Iterating a `Map` visits entries in the order
their keys were first inserted (removing a key forgets its position). The
default hasher is deterministic: it uses no random seed, so a program's
output never depends on hash values and repeated runs behave identically.
Strings hash with SipHash-1-3 under a fixed key. For maps filled from
untrusted input, `Map.seeded()` keys the hash with a random seed, so the
slots of the keys cannot be predicted (ADR 0034); `Map.withSeed(seed)`
takes a given seed. A seeded map still iterates in insertion order.

## 2.7 Fixed-size arrays

`[T; N]` is an array of exactly `N` elements stored inline.

```ofir
const grid: [u8; 4] = [0, 0, 0, 0]
const zeros: [int; 8] = [0; 8]
```

- A list literal or `[value; N]` is an array where an array type is
  expected; its length must be `N`, and the count of `[value; N]` is an
  integer literal.
- `a[i]` reads and writes an element, bounds checked; `for x in a` and
  `for i, x in a` read the elements.
- `&[T; N]` converts to `&[T]`, and a `[T]` parameter accepts an array
  (chapter 2.12).
- Stage 1 implements arrays whose element type is Copy; arrays of other
  types are E0901. Stage 0 has the type but no array values.

## 2.8 References

`&T` is a shared (read-only) reference and `&mut T` is an exclusive
(mutable) reference. References never own the value they point to.

Most code never writes these types: parameters are borrowed by default and
`mut`/`move` express the other cases (chapter 5.3). Explicit references are
for local borrows, functions that return a borrow and `ref struct` types
(chapter 5.6).

## 2.9 Copy and move types

A type is **Copy** if a bitwise copy is a complete, independent copy:

- all primitive types and `()`
- shared references `&T`
- fixed arrays `[T; N]` whose element type is Copy
- structs and enums declared with `copy` whose fields are all Copy
- `Option<T>` and `Result<T, E>` when their type arguments are Copy, and
  tuples whose elements are all Copy

```ofir
copy struct Vec2 {
    x: float
    y: float
}
```

All other types (`str`, `List`, `Map`, `Box`, and structs or enums that are
not marked `copy`) are **move** types. Chapter 5 defines what moving means.
A Copy type cannot define a `drop` method.

## 2.10 Type equivalence

Two types are equal if they have the same name and the same type arguments.
Ofir uses nominal typing: two structs with identical fields are different
types.

## 2.11 Tuples

A tuple groups a fixed number of values of possibly different types.

```ofir
fn divMod(a: int, b: int) (int, int) {
    return (a / b, a % b)
}

const (q, r) = divMod(17, 5)
const pair = (1, "one")
println("{pair.0}")
```

Tuples are Copy when all their elements are Copy. The empty tuple is the
unit type `()`.

## 2.12 Slices

A slice is a borrowed view of a contiguous sequence: a pointer and a length.

| Type | Meaning |
|------|---------|
| `&[T]` | read-only view of `T` elements |
| `&mut [T]` | mutable view of `T` elements |
| `&str` | read-only view of UTF-8 bytes |

```ofir
const xs = [10, 20, 30, 40]
const middle: &[int] = xs[1..3]     // [20, 30]
const s = "hello, world"
const hello: &str = s[0..5]
```

- `&List<T>` and `&[T; N]` convert implicitly to `&[T]`; `&str` borrowed
  from a `str` is already a slice.
- Slicing is bounds checked. Slicing a `str` panics if a bound is not on a
  UTF-8 character boundary.
- Slices are references, so they follow the rules of chapter 5.
- `&[T]` is the std type `Slice<T>` (`std/core/slice.of`); `xs[a..b]` is
  `xs.slice(a, b)` and `xs[a..]` is `xs.sliceFrom(a)`. Stage 0 treats
  `&str` as a `str` whose capacity is 0, like a literal.
- `&mut [T]` is the std type `SliceMut<T>`. `&mut list` (`list.asMutSlice()`)
  and `&mut array` convert to it; `view[i] = v` writes the element, and the
  slice borrows its source exclusively while it is used. Stage 1 implements
  it; stage 0 does not.
- As **parameter types**, `[T]` and `str` accept lists, arrays, slices and
  strings without writing `&`: `fn sum(xs: [int])` can be called as
  `sum(list)` or `sum(list[2..5])` (chapter 5.3).

## 2.13 Function types

`fn(A, B) R` is the type of a borrowed function value: a named function or
a closure. `move fn(A, B) R` is an owned function value that can be stored
and returned. See chapter 10.

```ofir
fn apply(f: fn(int) int, x: int) int {
    return f(x)
}

fn double(x: int) int {
    return x * 2
}

const y = apply(double, 21)
```

## 2.14 Type aliases

```ofir
type NodeId = u32
type Table = Map<str, List<int>>
```

An alias is another name for the same type, not a new type. To create a
distinct type, wrap it in a struct: `struct NodeId { value: u32 }`.

Aliases may take type parameters:

```ofir
type Pair<T> = (T, T)
type TableOf<V> = Map<str, V>
type Nested<T> = Pair<List<T>>
```

All arguments must be supplied at each use (`Pair<int>`); they are resolved
in the caller's scope. The alias body is resolved in its declaring module,
with its parameters replaced by those arguments. Aliases are transparent:
`Pair<int>` and `(int, int)` have the same identity, layout and operations.
The underlying type's bounds and storage restrictions still apply.
Direct and indirect cycles between alias bodies are errors, even through a
container. Nested uses such as `Pair<Pair<int>>` are valid. Alias parameters
do not declare bounds; bounds on the resulting type are checked at each use.

## 2.15 The never type

`!` is the type of expressions that never produce a value: `panic(...)`,
`return`, `break`, `continue` and infinite loops. It converts to any type,
which makes code like this valid:

```ofir
const n = match input {
    Option.Some(v) => v
    Option.None => panic("missing input")
}
```

## 2.16 Raw pointers

`*T` and `*mut T` are unchecked pointers, usable only in `unsafe` code
(chapter 11).

## 2.17 Sized and unsized types

Most types have a size known at compile time. Three kinds of types do not:

- `dyn I` (the size depends on the concrete type),
- `[T]` (a sequence of unknown length),
- the text behind a `&str`.

Unsized types can only be used as the type of a borrowed or `mut`
parameter (`xs: [int]`, `s: dyn Shape`), or behind a pointer: `&`, `&mut`,
`*`, `*mut` or `Box`. Such pointers are fat (chapter 13.4). Generic type parameters are
always sized; `&T`, `&mut T`, `Box<T>` and raw pointers are the built-in
types that also accept an unsized `T`.
