# 15. Compile-time code **[planned]**

Ofir has **no macros**. Code that must adapt to the shape of a type
(serialization, debug printing, comparisons, builders) is written as
ordinary generic code that inspects types **at compile time**. Like other
generic code, it is type-checked once, at its definition, and expanded for
each instance (ADR 0035). Stage 1 implements the `std.meta` predicates,
`meta.nameOf`, `comptime if`, `comptime panic` and `comptime for` over
`keyof T`, `meta.fieldsOf<T>()` and `meta.variantsOf<T>()` with `@tag`,
`comptime` expressions, `comptime fn` and module-level
`const X = comptime f()`, run by an AIR interpreter (15.5); compile-time
heap allocation is planned (roadmap 6.7a7).

Goals:

- the code that runs is ordinary, type-checked Ofir, readable and
  debuggable — no token-level transformations;
- no runtime reflection: everything is resolved during compilation and
  costs nothing at run time;
- libraries such as JSON encoders need no compiler support.

## 15.1 `comptime` blocks and constants

`comptime` marks code that the compiler executes during compilation. It may
use only constants, type information and pure functions (no I/O, no
allocation that escapes):

```ofir
const TABLE: [u32; 256] = comptime crc32Table()

comptime fn crc32Table() [u32; 256] { ... }    // usable only at compile time
```

A `comptime fn` can run only at compile time; an ordinary function can be
called from `comptime` code if its body only does what compile-time code
allows.

A constant whose initializer is a `comptime` expression may have any value
a `comptime` expression may have (15.5), not only a number, `bool` or `str`.
The interpreter computes it after type checking, so its value cannot be
used in another constant's initializer or as an array length (E0801). Each
use of the constant in a function is computed there; a constant that is
never used is never computed.

## 15.2 Type information

The module `std.meta` exposes compile-time descriptions of types:

| Function | Returns |
|----------|---------|
| `meta.fieldsOf<T>()` | the fields of struct `T`: `name`, `type`, `index`, attributes |
| `meta.variantsOf<T>()` | the variants of enum `T`: `name`, `index`, attributes |
| `meta.nameOf<T>()` | the type's name, as a constant `str` |
| `meta.isStruct<T>()`, `isEnum`, `isInteger`, `isSigned`, `isFloat`, ... | type classification |
| `meta.implements<T, I>()` | whether `T` satisfies interface `I` |

These values exist only at compile time. `meta.fieldsOf<T>()` needs `T` to
be a struct, or narrowed to one by `comptime if meta.isStruct<T>()` (15.3);
`meta.variantsOf<T>()` likewise needs an enum (E0364).

## 15.3 `comptime for` and `comptime if`

`comptime for` is unrolled by the compiler. It iterates over `keyof T`
(16.6), `meta.fieldsOf<T>()` or `meta.variantsOf<T>()`. Its body is
type-checked once, with the loop variable standing for an implicit key
parameter `K: keyof T` (16.2), and expanded once per field of each instance,
in declaration order, with `K` bound to that field. `break` and `continue`
are not allowed in it.

`comptime if` keeps only the branch whose condition is true for the
instance. The condition is built from `true`, `false`, constants, the
`meta` predicates and `field.index`, with `!`, `&&` and `||` (E0363
otherwise). When it does not depend on a type parameter, only the taken
branch is type-checked. Otherwise both branches are, and in the `then`
branch the predicates **narrow** the type parameters they name:

| Predicate | Adds to `T` |
|-----------|-------------|
| `meta.implements<T, I>()` | the bound `I` |
| `meta.isInteger<T>()` | `Copy`, `Eq`, `Ord`, `Hash`, `Clone`, `Debug`, `Display` and the integer operations |
| `meta.isSigned<T>()` | with `isInteger`, unary `-` |
| `meta.isFloat<T>()` | `Copy`, `Clone`, `Debug`, `Display` and the float operations |
| `meta.isStruct<T>()`, `meta.isEnum<T>()` | the use of `fieldsOf`, `variantsOf` |

Narrowing passes through `&&`; under `A || B` a parameter gets only what
both `A` and `B` give it, and `!` gives nothing.

**Numeric narrowing.** A parameter narrowed by `isInteger` or `isFloat`
takes the operators of the primitive types (chapter 4), with the semantics
of each instance's concrete type; no function call is hidden (9.9):

- integers: `+`, `-`, `*`, `/`, `%`, the bitwise operators and shifts,
  the comparisons, and the compound assignments; unary `-` also needs
  `meta.isSigned<T>()`, since `T` may be unsigned;
- floats: `+`, `-`, unary `-`, `*`, `/`, the comparisons and the compound
  assignments;
- under `isInteger<T>() || isFloat<T>()`: what both give, that is `+`,
  `-`, `*`, `/` and the comparisons;
- conversions: `x as T` from a number, and `value as U` to a numeric type
  `U`, with the range checks of `as` for each instance (2.1);
- literals: an integer literal is a `T` when it fits every integer type,
  `0` to `127`; a float literal is a `T` under `isFloat`. Other values are
  written `n as T`.

```ofir
import std.meta

fn sum<T>(items: &[T]) T {
    comptime if meta.isInteger<T>() || meta.isFloat<T>() {
        let total: T = 0 as T
        for x in items {
            total += x
        }
        return total
    } else {
        comptime panic("sum: {meta.nameOf<T>()} is not a number")
    }
}
```

Overflow, division by zero and NaN behave as in the concrete type: `sum`
of a `List<u8>` panics on overflow like any `u8` addition.

```ofir
import std.meta

pub fn toJson<T>(value: T, out: mut str) {
    comptime if meta.isInteger<T>() || meta.isFloat<T>() {
        out.push("{value}")
    } else comptime if meta.isStruct<T>() {
        out.push("\{")
        comptime for field in meta.fieldsOf<T>() {
            comptime if field.index > 0 {
                out.push(",")
            }
            out.push("\"{field.attribute("json") ?? field.name}\":")
            toJson(field.get(value), out)
        }
        out.push("\}")
    } else {
        comptime panic("toJson: unsupported type {meta.nameOf<T>()}")
    }
}

struct User {
    @tag("json", "user_id")
    id: int
    name: str
}
```

- `field.name` is the field's name (`str`), `field.index` its position
  (`int`) and `field.attribute(key)` its `@tag` value (`Option<str>`).
- `field.get(value)` is `value[K]`: its type is `T[K]`, the field's type in
  each instance. `toJson(field.get(value), out)` calls the instance of
  `toJson` for that type.
- Over `meta.variantsOf<T>()`, the loop variable stands for one variant of
  `T`, in declaration order: `variant.name`, `variant.index` and
  `variant.attribute(key)` are as for fields, and `variant.matches(value)`
  is a `bool`, whether `value` (a `T` or a `&T`) is that variant, decided
  at run time. A variant has no `get`: its fields are reached with `match`.

```ofir
fn variantName<T>(value: &T) str {
    comptime if meta.isEnum<T>() {
        comptime for variant in meta.variantsOf<T>() {
            if variant.matches(value) {
                return variant.attribute("json") ?? variant.name
            }
        }
    }
    return "?"
}
```

- `comptime panic(...)` has type `never`. Its message is a string literal
  whose interpolations are compile-time `str` values. Expanding an instance
  that keeps it is a compile error at the call that created the instance,
  with the given message (E0814).

## 15.4 Attributes as metadata

`@tag("key", "value")` attaches a string to a field, variant or type. Tags
have no effect on compilation; compile-time code reads them through
`field.attribute("key")` (an `Option<str>`). Other attributes keep the
meanings of chapter 12.1.

## 15.5 Limits

- The value of a `comptime` expression is a number, `bool`, `str`, array,
  tuple, struct or enum of those (E0818); what its functions allocate does
  not escape.
- Compile-time execution has a step budget (default 10 million operations,
  configurable with `@comptimeBudget(n)` on the enclosing declaration), so
  a buggy loop is a compile error instead of a hang. A panic, an
  arithmetic or bounds error, or running out of budget is E0816.
- `comptime` code is deterministic: it cannot read files, the clock, the
  environment or random numbers, nor call `extern` functions (E0815).
  Builds stay reproducible (required for the self-hosting check).
- A `comptime fn` called from code that runs at run time is E0817.
