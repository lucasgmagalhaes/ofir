# `std.mem`, `std.math`, `std.random`, `std.testing`, `std.fmt`

## `std.mem`

| Item | Description | S0 |
|------|-------------|:--:|
| `fn swap<T>(a: mut T, b: mut T)` | | ✓ |
| `fn replace<T>(dest: mut T, value: move T) T` | returns the old value | ✓ |
| `fn take<T: Default>(dest: mut T) T` | leaves `T.default()` behind | |
| `fn sizeOf<T>() int`, `fn alignOf<T>() int` | also in the prelude | ✓ |
| `struct UnsafeCell<T>` | the interior mutability primitive (spec 5.8) | |
| `struct Cell<T>` (T Copy) with `get`, `set` | | |
| `unsafe fn alloc(size: int, align: int) *mut u8`, `free`, `realloc` | raw allocation | ✓ |

### `std.mem.atomic`

The atomic intrinsics (ADR 0042, spec 14.4): unsafe, each lowered to one
instruction on a `*mut T` of an integer, `bool` or raw pointer. The
ordering is a constant `Ordering.X` (E0820) that the operation allows
(E0821).

| Item | Description | S0 |
|------|-------------|:--:|
| `enum Ordering` | `Relaxed`, `Acquire`, `Release`, `AcqRel`, `SeqCst` (C11) | ✓ |
| `fn atomicLoad<T>(p: *mut T, order: Ordering) T` | `Relaxed`, `Acquire`, `SeqCst` | ✓ |
| `fn atomicStore<T>(p: *mut T, value: T, order: Ordering)` | `Relaxed`, `Release`, `SeqCst` | ✓ |
| `fn atomicSwap<T>(p: *mut T, value: T, order: Ordering) T` | returns the old value | ✓ |
| `fn atomicFetchAdd<T>`, `fn atomicFetchSub<T>` | integers; return the old value | ✓ |
| `fn atomicCompareExchange<T>(p, expected: T, desired: T, order) T` | returns the value found | ✓ |
| `fn fence(order: Ordering)` | any ordering but `Relaxed` | ✓ |

## `std.math`

The elementary functions are methods of `f32` and `f64` (core.md);
`std.math` holds what is not a method (ADR 0057).

| Item | Notes | S0 |
|------|-------|----|
| `PI`, `TAU`, `E`, `SQRT_2`, `LN_2`, `LN_10` | `f64` constants | ✓ |
| `INFINITY`, `NEG_INFINITY`, `NAN` | `f64`; test NaN with `isNan()` | ✓ |
| `EPSILON`, `MAX`, `MIN_POSITIVE` | the limits of `f64` | ✓ |
| `fn approxEq(a: f64, b: f64, relTol: f64, absTol: f64) bool` | equal up to a relative or absolute tolerance; NaN never | ✓ |
| `fn sum(values: &[f64]) f64` | Neumaier-compensated sum | ✓ |
| `fn gcd(a: int, b: int) int`, `fn lcm(a: int, b: int) int` | non-negative results | ✓ |
| `fn isqrt(n: int) int` | exact; **panics** when `n < 0` | ✓ |

```ofir
import std.math

fn main() {
    const angle = math.PI / 6.0
    println("{angle.sin()}")                                 // 0.49999999999999994
    println("{math.approxEq(angle.sin(), 0.5, 1e-12, 0.0)}") // true
}
```

## `std.random`

An ordinary, seeded generator for simulations, tests, sampling and games
(ADR 0057). It is **not** for secrets: keys, tokens and nonces need the
operating system's entropy (API-09).

| Item | Notes | S0 |
|------|-------|----|
| `struct Rng` | xoshiro256\*\*, seeded through SplitMix64 | ✓ |
| `Rng.seeded(seed: u64) Rng` | the same sequence for the same seed, everywhere | ✓ |
| `Rng.fromTime() Rng` | seeded from the clock; guessable | ✓ |
| `nextU64() u64` | 64 random bits | ✓ |
| `nextInt(lo: int, hi: int) int` | in `lo..hi`, without modulo bias; **panics** when `hi <= lo` | ✓ |
| `nextF64() f64` | in [0, 1), 53 random bits | ✓ |
| `nextBool() bool`, `chance(p: f64) bool` | a fair coin; true with probability `p` | ✓ |
| `normal(mean: f64, stdDev: f64) f64` | Box-Muller | ✓ |
| `exponential(rate: f64) f64` | mean `1 / rate` | ✓ |
| `shuffle<T>(items: mut List<T>)` | Fisher-Yates, in place | ✓ |
| `choose<T>(items: &[T]) Option<&T>` | `None` for no items | ✓ |

```ofir
import std.random.Rng

fn main() {
    let rng = Rng.seeded(2026)
    const roll = rng.nextInt(1, 7)
    println("{roll >= 1 && roll <= 6}")   // true
}
```

## `std.testing`

| Item | Description |
|------|-------------|
| `fn assert(condition: bool)` | panics when `condition` is false |
| `fn assertEq<T: Eq + Debug>(left: T, right: T)` | panics when `left != right`, printing both |
| `fn assertNe<T: Eq + Debug>(left: T, right: T)` | panics when `left == right`, printing both |
| `fn assertStrEq(actual: str, expected: str)` | names the first line that differs and quotes both versions |
| `fn assertApprox(actual: f64, expected: f64, tolerance: f64)` | an absolute tolerance; NaN never matches |
| `fn assertContains(text: str, part: str)` | panics unless `part` occurs in `text` |
| `fn assertGolden(actual: str, path: str)` | compares with a file; writes it when `OFIR_UPDATE_GOLDEN` is set and not empty |
| `struct TempDir` | `create(prefix)`, `path()`, `join(name)`, `remove(move self)` deletes the tree |
| `struct Bencher` with `fn run(mut self, f: fn())` | used by `@bench`; `iterations()` and `nanosPerIter()` return the last measurement |

None of them is in the prelude: a module imports them (ADR 0036). The
asserts written in Ofir (`assertStrEq` to `assertGolden`) work in both
stages and report a location inside `std.testing`; their messages carry the
values (ADR 0061). `TempDir.create` uses `TMPDIR`, `TEMP` or `TMP`, else
`/tmp`; stage 0 has no drop hooks, so a test calls `remove`.

## `std.fmt`

Interpolation and its specifiers (spec 1.7) are lowered by the compiler;
users normally only implement `Display.format` (spec 9.9) and call
`out.push(...)` on the `out: mut str` parameter they receive. `std.fmt`
(ADR 0061, both stages) formats with widths and precisions known only at
run time. Widths count Unicode scalar values.

| Item | Description |
|------|-------------|
| `fn padStart(text: str, width: int, fill: str) str` | pads on the left; wider text is unchanged |
| `fn padEnd(text: str, width: int, fill: str) str` | pads on the right |
| `fn center(text: str, width: int, fill: str) str` | pads both sides, the extra fill on the right |
| `fn fixed(x: f64, digits: int) str` | `digits` (0 to 9) after the point, rounded half away from zero |
| `fn grouped(n: int, separator: str) str` | digits grouped by three: `1,234,567` |
| `fn radix(n: int, base: int) str` | base 2 to 36, lowercase letters |
| `fn bytes(n: int) str` | binary units: `512 B`, `1.5 KiB`, `3 MiB` |
| `fn duration(ns: int) str` | `ns`, `µs`, `ms`, `s`, `min` or `h`: `1.25 s` |
| `struct Table` | `new()`, `alignRight(column)`, `row(cells)`, `render()`: columns aligned, two spaces apart |

`fixed` writes NaN, infinities and values at or above `2^53 / 10^digits`
as interpolation does. `bytes` and `duration` keep at most two digits
after the point and drop trailing zeros.
