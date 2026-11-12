# 9. Interfaces

An interface names a set of methods. A type **satisfies** an interface when
it has all of those methods. As in Go, satisfaction is **implicit**: there
is no `implements` declaration. Unlike Go, interfaces can be used both with
**static dispatch** (generics, zero cost) and **dynamic dispatch** (`dyn`,
one indirect call).

## 9.1 Declaring interfaces

```ofir
interface Shape {
    fn area(self) float
    fn name(self) str
}
```

- `Self` is the type satisfying the interface.
- Methods are written as inside a struct (chapter 3.6): `self`, `mut self`
  or `move self` as the first parameter.
- Associated functions without `self` are allowed: `fn default() Self`.

## 9.2 Satisfying interfaces

```ofir
struct Circle {
    radius: float

    fn area(self) float {
        return 3.14159 * self.radius * self.radius
    }

    fn name(self) str {
        return "circle"
    }
}

// Circle now satisfies Shape. Nothing else is needed.
```

A method satisfies an interface method when the name, the mode of `self`,
the parameter types and the return type match exactly.

### Explicit assertion

To document intent and get an error at the type's declaration if a method is
missing, a type can assert that it satisfies an interface:

```ofir
struct Circle: Shape + Display {
    radius: float

    ...
}
```

The assertion does not change behavior; it only adds a compile-time check.

## 9.3 Default methods

An interface method can have a body. Types get it for free unless they define
their own:

```ofir
interface Shape {
    fn area(self) float
    fn name(self) str

    fn describe(self) str {
        return "{self.name()} with area {self.area()}"
    }
}
```

A type gets a default method where it is used through a bound, and, as a
method of its own, from the interfaces it asserts (`struct Circle: Shape`)
and those they embed. Satisfying an interface without asserting it does not
add its default methods to the type.

Two defaults for the same method conflict when they come from different
interfaces and no interface involved declares the method without a body.
The conflict is an error (E0329) where it would have to be resolved:

- in an interface that embeds both, unless it declares the method itself;
- in a type that asserts both, unless it declares the method itself;
- in a call through a type parameter bounded by both (`T: A + B`).

```ofir
struct Robot: Named + Greeter {     // both give `hello`
    fn name(self) str {
        return "r2"
    }

    fn hello(self) str {          // required: resolves the conflict
        return "beep {self.name()}"
    }
}
```

## 9.4 Embedding

An interface can include other interfaces:

```ofir
interface Reader {
    fn read(mut self, buf: mut [u8]) Result<int, IoError>
}

interface Writer {
    fn write(mut self, data: [u8]) Result<int, IoError>
}

interface ReadWriter {
    Reader
    Writer
}
```

The methods of an embedded interface become methods of the one that embeds
it. A method that reaches an interface more than once, through two embedded
interfaces or one embedded interface and a declaration of its own, must
have the same `self` mode and signature each time; the declarations then
merge (E0328 otherwise). An interface cannot embed itself, directly or
through the interfaces it embeds (E0327). A recursive type, such as a tree
whose children are a `List<Tree>`, can still satisfy and assert interfaces:
only interfaces cannot be cyclic.

## 9.5 Generic interfaces

```ofir
interface Iterator<T> {
    fn next(mut self) Option<T>
}

interface From<T> {
    fn from(value: move T) Self
}
```

The parameters come after `Self`. A bound, an assertion or an interface
parameter type gives them, one each (E0302): `S: Iterator<int>`,
`struct Lines: Iterator<str>`, `it: Iterator<T>`. A type satisfies
`I<A>` when its methods match those of `I` with `Self` and the
parameters replaced (E0335, E0332):

```ofir
struct Countdown: Iterator<int> {
    n: int

    fn next(mut self) Option<int> { ... }
}

fn count<I, T>(it: mut I) int where I: Iterator<T> { ... }

count(Countdown{n: 3})   // `T` is `int`, from `next`
```

- A parameter that appears only in a bound's arguments is inferred from
  the methods of the type given for the bounded parameter.
- A type parameter bounded by an interface with
  `fn next(mut self) Option<T>` can be iterated with `for` (spec 4).
- `Iterator<T>` is in the prelude (`std.core.iter`).
- Not supported yet (E0901): `dyn` of a generic interface and embedding
  one.

## 9.6 Static dispatch

An interface used as a bound is resolved at compile time. Calls are direct
and can be inlined:

```ofir
fn totalArea<S: Shape>(shapes: [S]) float {
    let sum = 0.0
    for s in shapes {
        sum += s.area()
    }
    return sum
}
```

### Interfaces as parameter types

An interface written as the type of a parameter, or as part of it, is an
anonymous type parameter bound by that interface (ADR 0029). These two
functions are the same:

```ofir
fn totalArea(shapes: [Shape]) float { ... }
fn totalArea<S: Shape>(shapes: [S]) float { ... }
```

- It is allowed anywhere in the type of a parameter of a function or a
  method (`s: Shape`, `s: mut Shape`, `items: List<Shape>`), and nowhere
  else: not in return types, fields, `const`, type arguments, `fn(...)`
  types or closure parameters (E0356).
- Each occurrence is a type parameter of its own:
  `fn f(a: Shape, b: Shape)` accepts a `Circle` and a `Square`. Values that must share a
  type, or a type used again (in the return type, a `where` clause), need
  a named type parameter.
- One interface per position: several bounds need a named parameter,
  `<S: Shape + Display>`.
- The anonymous parameters come after the named ones and are always
  inferred: type arguments written at a call give only the named ones.

`s: Shape` is static and `s: dyn Shape` dynamic; only `dyn` holds values
of different types together (`List<Box<dyn Shape>>`).

## 9.7 Dynamic dispatch with `dyn`

`dyn Interface` is a type whose concrete type is known only at runtime.
It is unsized, so it is used as a parameter type or behind a pointer:

| Use | Meaning |
|-----|---------|
| `s: dyn Shape` (parameter) | borrows any value whose type satisfies `Shape` |
| `s: mut dyn Shape` (parameter) | borrows it mutably |
| `Box<dyn Shape>` | owned, e.g. in a list of different shapes |
| `&dyn Shape` | explicit reference (chapter 5.6) |

```ofir
fn draw(s: dyn Shape) {
    println(s.describe())
}

draw(Circle{radius: 1.0})
```

```ofir
const shapes: List<Box<dyn Shape>> = [
    Box.new(Circle{radius: 1.0}),
    Box.new(Square{side: 2.0}),
]

for s in shapes {
    println(s.describe())
}
```

A `dyn` value is a **fat pointer**: a data pointer plus a pointer to a
virtual table. The table holds, in order, the drop function, the size, the
alignment and the type id of the concrete type (for type switches, 9.8),
then one entry per method callable through `dyn`, in declaration order.
Converting a concrete pointer to a `dyn` pointer is implicit when the
expected type is known, in generic arguments too
(`list.push(Box.new(Circle{radius: 1.0}))` on a `List<Box<dyn Shape>>`).

A `dyn` parameter also takes a `dyn` value: `s: dyn Shape` takes a
`&dyn Shape`, a `&mut dyn Shape` or a `Box<dyn Shape>`, and
`s: mut dyn Shape` a `&mut dyn Shape` or a mutable `Box<dyn Shape>`;
the box is borrowed, not moved. A `mut dyn` parameter needs a mutable
value (E0315), and a `&dyn` does not become a `&mut dyn` (E0300). A `dyn`
value cannot be taken by `move`: a function that takes ownership takes a
`Box<dyn I>`.

Converting a `dyn` value to another interface, even one it embeds
(`dyn Both` to `dyn Named`), is **[planned]**; until then it is a type
mismatch (E0300). `dyn` of a generic interface (`dyn Iterator<int>`) is
**[planned]** too, and rejected with E0901.

### Dyn compatibility

An interface can be used with `dyn` only if every method:

- takes `self` or `mut self` (no `move self`, no associated
  functions);
- has no type parameters of its own;
- does not mention `Self` in parameter or return types.

Methods that break these rules may still exist if they have a default
body; they are simply not callable through `dyn`.

## 9.8 Type switches

The concrete type behind a `dyn` value can be tested with `match`:

```ofir
match shape {
    c: Circle => println("circle of radius {c.radius}")
    _ => println("another shape")
}
```

This requires the concrete type to be known to the compiler at the match
site; the vtable carries a unique type id. A type switch needs a `_` arm,
since the set of types behind a `dyn` is open (E0500). A type test that
names a type that does not satisfy the interface can never match (E0354).
A type test binds like any pattern (chapter 5.5): on a borrowed `dyn`
value the binding borrows; on an owned `Box<dyn I>` it takes the value by
move and the box is freed.

### `any`

`any` (ADR 0024, ADR 0039) holds a value of any type. It is a builtin
`dyn` type: `v: any` borrows a value, `&any` and `*any` point at one and
`Box<any>` owns one, as `dyn I` does for an interface `I` (9.7). `dyn any`
is an error (E0365). It is declared in `std.core`, in the prelude:

```ofir
pub interface any {
    fn typeName(self) str
}
```

Every type satisfies `any`, except the types that hold references (`&T`,
a `ref struct`, a type with such a field), which cannot be stored (chapter
5.6, E0702). The compiler provides `typeName` for every type, which cannot
declare its own; it returns the type's name as written in messages:
`"int"`, `"List<str>"`, `"app.Config"`. The value is recovered with a type
switch:

```ofir
fn describe(v: any) str {
    return match v {
        n: int => "the int {n}"
        s: str => "the str {s}"
        p: Point => "a point at {p.x}, {p.y}"
        _ => "a {v.typeName()}"
    }
}

fn takeConfig(value: move Box<any>) Option<Config> {
    return match value {
        c: Config => Option.Some(c)   // moved out of the box
        _ => Option.None              // the box is dropped with its value
    }
}
```

`if const c: Config = value { ... }` tests a single type. There is no `is`
operator and no reflection: a type test, which the compiler checks, is the
only way to the concrete value. Two `any` values cannot be compared or
hashed.

## 9.9 Built-in interfaces

These interfaces are defined in `std.core` and are part of the prelude.
The compiler **derives them automatically** for structs and enums whose
fields all satisfy them, unless the type defines the method itself.

| Interface | Methods | Used by |
|-----------|---------|---------|
| `Eq` | `fn eq(self, other: Self) bool` | `==`, `!=` |
| `Ord` | `fn cmp(self, other: Self) Ordering` | `<`, `<=`, `>`, `>=`, sorting |
| `Hash` | `fn hash(self, h: mut Hasher)` | `Map` keys |
| `Clone` | `fn clone(self) Self` | `.clone()` |
| `Display` | `fn format(self, out: mut str)` | string interpolation, `println` |
| `Debug` | `fn debug(self, out: mut str)` | `{x:?}` in interpolation |
| `Drop` | `fn drop(mut self)` | destruction (chapter 5.2) |
| `Iterator<T>` | `fn next(mut self) Option<T>` | `for` loops |
| `From<T>` | `fn from(value: move T) Self` | error conversion in `?` (chapter 6.2) |
| `Default` | `fn default() Self` | `..T.default()` in struct literals, `mem.take` |

`Display` is **not** derived automatically; a type chooses how it is shown.
`Debug` is derived for every type whose parts have it; `{x:?}` shows its
debug representation, close to how the value is written in code:

| Value | Representation |
|-------|----------------|
| numbers, `bool`, `()` | as interpolated: `42`, `1.5`, `true`, `()` |
| `str` | quoted, with `\"`, `\\`, `\n`, `\t`, `\r`, `\0` and `\xNN` escapes |
| tuple | `(1, "a")`, `(7,)` |
| struct | `Point { x: 1, y: 2 }`; empty: `Empty {}`; tuple struct: `Meters(5)` |
| enum | the variant and its fields in order: `Some(3)`, `Rect(3, 4)`, `None` |
| union | the member it holds: `3`, `"hi"` (chapter 17) |
| `List`, array | `[1, 2, 3]` |
| `Map` | `{"a": 1, "b": 2}`, in insertion order |
| `Box<T>`, `&T` | the value it points to |

A type that declares `fn debug(self, out: mut str)` appends its own
representation to `out`; a method named `debug` of another shape, or a
`drop` method, blocks the derivation. Functions and raw pointers have no
debug representation. `Debug` can bound a type parameter
(`fn show<T: Debug>(x: T)`). Stage 1 only.

`Eq`, `Ord` and `Hash` are derived as follows:

- A struct compares and hashes its fields in declaration order. An enum
  compares its variants first, in declaration order, then the fields of
  the variant. So `<` on two structs is decided by the first fields that
  differ.
- Tuples satisfy each interface when every element does. Floats satisfy
  none of them (chapter 2.1), so a struct with a float field is not
  derived either.
- A type with a `drop` method (`List`, `Box`, `Map`) owns a resource: its
  fields say nothing about its value, so nothing is derived for it.
- A type that declares `fn eq(self, other: Self) bool` or
  `fn cmp(self, other: Self) Ordering` uses that method for `==` or `<`.
  A method named `eq` or `cmp` of another shape does not count, and blocks
  the derivation of its interface.
- A declared method is never mixed with derived ones, which could
  disagree with it. A type that declares `eq` derives neither `Ord` nor
  `Hash`; a type that declares `cmp` derives neither `Eq` nor `Hash`, so
  it declares `eq` too for `==`. Neither is a `Map` key until it can
  declare `hash` (which needs `Hasher`, planned).

The interfaces have contracts, for every `a`, `b` and `c` of the type:

- `Eq`: `a == a`; `a == b` exactly when `b == a`; `a == b` and `b == c`
  give `a == c`; `a != b` is `!(a == b)`.
- `Ord`: exactly one of `a < b`, `a == b`, `a > b` holds, where `==` is
  the type's `Eq` when it has one; `<` is transitive; `a <= b` is
  `a < b || a == b`.
- `Hash`: `a == b` gives equal hashes, under any seed of a `Map` (chapter
  2.6); unequal values may share a hash.

Integers, `bool`, `str`, `()` and the derived interfaces keep them,
provided the fields keep them. Floats keep them except when a NaN takes
part (chapter 2.1). The compiler does not check a declared `eq` or `cmp`:
one that breaks its contract makes `==`, `<`, `list.sort()` and `Map`
lookups give unspecified results, without breaking memory safety.

```
struct Version {
    major: int
    minor: int
}

const a = Version{major: 1, minor: 2}
const b = Version{major: 1, minor: 10}
a < b                                  // true: `major` ties, `minor` decides
let seen: Map<Version, bool> = Map.new()
seen.insert(a, true)
```

A `for` loop accepts built-in collections (`List`, arrays, slices, `str`,
`Map`) and any value whose type satisfies `Iterator<T>`. User collections
expose iteration through a method returning a `ref struct` iterator
(chapter 5.6), e.g. `for x in tree.iter() { }`.

Operator overloading beyond these interfaces (for example `+` on user
types) is intentionally not supported: an operator never hides a function
call that the reader cannot predict.

## 9.10 Coherence

Methods are declared inside the body of their type (chapter 3.6), so a
type's methods are all known where the type is declared. As a consequence, whether a type satisfies an interface never depends
on which modules are imported.
