# 0047. Array operations; range values and 128-bit integers deferred

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-21 (docs/design/language-completeness.md) groups three gaps and asks
to decide each one on its own: range values, 128-bit integers and array
operations. Before this ADR:

- A fixed array `[T; N]` had literals, indexing, `for` and conversion to
  `&[T]`, but no methods: `a.len()`, `a[1..3]`, `a == b` and `{a:?}` were
  errors (E0301, E0313, E0314).
- `==` on `&[T]` was accepted and compared the pointer and the length, so
  two slices with equal elements of different lists were not equal.
- `a..b` was only allowed in `for` and in an index (E0901 elsewhere).
- `i128` and `u128` did not exist (E0200).

## Decision

- **Arrays have the operations of `&[T]`.** A method of `&[T]` called on
  an array, or on a reference to one, receives a slice of its N elements:
  `a.len()`, `a.get(i)`, `a.first()`. `a[lo..hi]` and `a[lo..]` slice it.
  No copy is made.
- **`==` on slices and arrays compares elements.** `a == b` on `&[T]` holds
  when the lengths match and the elements are equal in order, through
  `std.core.slice.sliceEqual`; it needs `T: Eq`. Arrays compare the same
  way. This fixes the pointer comparison. `&mut [T]` has no `==`: compare
  `s.asSlice()`. Slices have no order and no hash.
- **`{x:?}` prints the elements** of a slice or an array in brackets, like a
  list (`Slice.debug`).
- **128-bit integers are deferred.** Their C ABI differs between targets
  (alignment of `__int128`), and formatting, parsing and checked
  arithmetic would need new runtime code that nothing in the compiler or
  std uses yet. E0200 on `i128`/`u128` gets a help that suggests two `u64`
  halves.
- **Range values are deferred.** A first-class `Range` would need a std
  type, a lang item in both stages and a decision on `a..=b` overflow; the
  only uses found are `for` and slicing, which already work. ADR 0040
  keeps std free of lazy adapter chains, which removes most other uses.
  E0901 on a range value gets a help that points to `for` and `xs[a..b]`.

## Consequences

- Code that used `==` on slices gets element comparison; a slice of a type
  without `Eq` is now E0313 for `==`.
- Arrays stay limited to Copy element types in stage 1 (spec 2.7).
- Revisit 128-bit integers when a use needs them (hashing, crypto, wide
  multiplication), and range values together with a decision on
  iterators as values.
