# Prelude and `std.core`

Everything on this page is available without imports.

## `Option<T>`

```ofir
enum Option<T> { Some(value: T)  None }
```

| Method | Description | S0 |
|--------|-------------|:--:|
| `fn isSome(self) bool` | | ✓ |
| `fn isNone(self) bool` | | ✓ |
| `fn unwrap(move self) T` | **panics** on `None` | ✓ |
| `fn expect(move self, msg: str) T` | **panics** with `msg` on `None` | ✓ |
| `fn unwrapOr(move self, default: move T) T` | | ✓ |
| `fn asRef(self) Option<&T>` | borrow the content | ✓ |
| `fn take(mut self) Option<T>` | leaves `None` behind | ✓ |
| `fn okOr<E>(move self, err: move E) Result<T, E>` | | ✓ |
| `fn map<U>(move self, f: fn(move T) U) Option<U>` | | |
| `fn unwrapOrElse(move self, f: fn() T) T` | | |

## `Result<T, E>`

```ofir
enum Result<T, E> { Ok(value: T)  Err(error: E) }
```

| Method | Description | S0 |
|--------|-------------|:--:|
| `fn isOk(self) bool` | | ✓ |
| `fn isErr(self) bool` | | ✓ |
| `fn unwrap(move self) T` | **panics** on `Err` (requires `E: Debug`) | ✓ |
| `fn expect(move self, msg: str) T` | **panics** with `msg` | ✓ |
| `fn unwrapOr(move self, default: move T) T` | | ✓ |
| `fn ok(move self) Option<T>` | | ✓ |
| `fn mapErr<F>(move self, f: fn(move E) F) Result<T, F>` | | |
| `fn map<U>(move self, f: fn(move T) U) Result<U, E>` | | |

Stage 0: `asRef` waits for references (roadmap 2.21), and `Result.unwrap`
panics with a fixed message until `Debug` (bounds); `err()` returns the
error as an Option.

`opt ?? default` (spec 4.8) is the operator form of `unwrapOr` with a lazily
evaluated default.

**Error conversion in `?`** (stage 1): if the function returns
`Result<_, F>` and `F` satisfies `From<E>`, `?` converts the error with
`F.from(e)`. Otherwise both error types must be equal.

## `Ordering`

```ofir
copy enum Ordering { Less  Equal  Greater }
```

Methods: `isLt()`, `isEq()`, `isGt()`, `reverse()`, and
`then(other: Ordering) Ordering` for lexicographic comparison.

## Type operator aliases

The aliases of spec 16.7, in `std.core.types` (stage 1 only; stage 0 keeps
them opaque and rejects a use of them with E0901):

```ofir
type Unwrap<T> = if T is Option<infer U> { U } else { T }
type Partial<T> = { [K in keyof T]?: T[K] }
type Required<T> = { [K in keyof T]: Unwrap<T[K]> }
type Pick<T, K: keyof T> = { [P in K]: T[P] }
type Omit<T, K: keyof T> = { [P in keyof T - K]: T[P] }
type Record<K, V> = { [P in K]: V }
```

## Interfaces

Defined in spec chapter 9.9: `Eq`, `Ord`, `Hash`, `Clone`, `Display`,
`Debug`, `Drop`, `Iterator<T>`, `Default`, `From<T>`, and the markers `Send`
and `Sync`.

```ofir
interface Hash {
    fn hash(self, h: mut Hasher)
}

pub struct Hasher {                     // deterministic (spec 2.6)
    ...

    pub fn write(mut self, bytes: [u8])
    pub fn writeInt(mut self, v: int)
    pub fn finish(self) u64
}
```

## Integer methods

For every integer type `I`:

| Method | Description | S0 |
|--------|-------------|:--:|
| `wrappingAdd/Sub/Mul(b: I) I` | modular arithmetic | ✓ |
| `checkedAdd/Sub/Mul/Div(b: I) Option<I>` | `None` on overflow | ✓ |
| `saturatingAdd/Sub/Mul(b: I) I` | clamps to the range | |
| `wrappingCast<T>()`, `saturatingCast<T>()`, `checkedCast<T>()` | spec 2.1 | ✓ |
| `divFloor(b: I) I`, `remEuclid(b: I) I` | other division conventions | |
| `abs() I` | **panics** on `MIN` for signed types | ✓ |
| `min(b: I) I`, `max(b: I) I`, `clamp(lo: I, hi: I) I` | | ✓ |
| `pow(exp: u32) I` | checked | ✓ |
| `countOnes() int`, `leadingZeros() int`, `trailingZeros() int` | | |
| `I.MIN`, `I.MAX` | constants | ✓ |
| `I.parse(s: str) Result<I, ParseError>` | decimal, optional sign | ✓ |

## Float methods

For `f32` and `f64` (ADR 0057). `f32` calls the single-precision C
function, so it never widens to `f64`. None of them panics: outside its
domain a function gives NaN or an infinity, as IEEE 754 and C do.

| Method | Notes | S0 |
|--------|-------|----|
| `sqrt()`, `abs()`, `floor()`, `ceil()`, `round()`, `trunc()`, `fract()` | exact; `round` takes halfway cases away from zero | ✓ |
| `min(b)`, `max(b)` | when one is NaN, the other | ✓ |
| `clamp(lo, hi)` | NaN stays NaN | ✓ |
| `sin()`, `cos()`, `tan()`, `asin()`, `acos()`, `atan()`, `atan2(x)` | radians; `y.atan2(x)` | ✓ |
| `sinh()`, `cosh()`, `tanh()` | | ✓ |
| `exp()`, `exp2()`, `ln()`, `log2()`, `log10()` | `ln(0)` is -infinity | ✓ |
| `powf(e)`, `powi(n: i32)`, `cbrt()`, `hypot(b)` | `hypot` does not overflow in the squares | ✓ |
| `mulAdd(a, b)` | `self * a + b` with one rounding | ✓ |
| `copySign(sign)`, `signum()`, `isSignNegative()` | see the sign of -0.0 | ✓ |
| `isNan()`, `isFinite()`, `isInfinite()` | | ✓ |
| `F.parse(s: str) Result<F, ParseError>` | as C's `strtod` reads it | ✓ |

The float constants (`PI`, `INFINITY`, `NAN`, `EPSILON` and others) are in
`std.math` (utilities.md).
