# `std.mem`, `std.math`, `std.testing`, `std.fmt`

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

Constants `PI`, `E`, `TAU`; functions `hypot(x, y)`, `min`, `max` over
floats. Most operations are methods on the float types (core.md).

## `std.testing`

| Item | Description |
|------|-------------|
| `fn assert(condition: bool)` | panics when `condition` is false |
| `fn assertEq<T: Eq + Debug>(left: T, right: T)` | panics when `left != right`, printing both |
| `fn assertNe<T: Eq + Debug>(left: T, right: T)` | panics when `left == right`, printing both |
| `struct Bencher` with `fn run(mut self, f: fn())` | used by `@bench` |

None of them is in the prelude: a module imports them (ADR 0036).

## `std.fmt`

Implements interpolation. Users normally only implement `Display.format`
(spec 9.9) and call `out.push(...)` on the `out: mut str` parameter they receive. Format
specifiers (spec 1.7) are applied by `std.fmt` after the value is formatted.
