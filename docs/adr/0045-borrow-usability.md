# 0045. Borrow usability: reborrows and views of part of a sequence

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-19 asks that the common borrow shapes be usable and sound: mutable
slices, reborrows, disjoint fields and reference returns, each with an
accepted case and a rejected one. ADR 0043 deferred slice patterns until
slice borrows were settled. The state before this ADR, in the self-hosted
borrow checker:

- **Disjoint fields** (`&mut p.a` while `p.b` is used) and **reference
  returns** (one source parameter, E0702) were already checked and tested.
- **Reborrows** were not tracked: a place reached through a reference
  (`*r`, `r.field`, `r[i]`) had no owner, so `&mut *r` followed by a use
  of `r` and then of the reborrow was accepted, and so were writes through
  `r` while a shared reborrow was live.
- **Views of part of a sequence** were missing: `&mut xs[a..b]` was a type
  error, `&xs[a..b]` did not lower in stage 1, and `xs[a..b]` borrowed
  nothing, so a view could outlive a `push` that moved the elements.
- `&mut str` appeared in a spec example but is not a type.

## Decision

- **A reborrow borrows the reference.** A borrow of, or a write to, a place
  reached through a local reference counts as an access to that reference:
  `&mut *r` borrows `r` mutably until its last use, `&*r` shares it, and
  `*r = v` or `r.push(v)` writes or mutably borrows it. Passing a reference
  to a function reborrows it for the call only, so `bump(r); bump(r)` is
  accepted. One statement reports one conflict per place: a read gives way
  to the borrow or write at the same spot.
- **`&mut xs[a..b]` is a `&mut [T]`** (`SliceMut<T>`): the call
  `xs.sliceMut(a, b)` or `xs.sliceFromMut(a)`, methods of `List` (on
  `mut self`) and `SliceMut`. A list must be mutable; a `&[T]` has no
  mutable views (E0301). `&xs[a..b]` stays a borrow of the `&[T]` view.
  Both borrow `xs`, mutably or shared, until the view's last use.
- **Ranges are not compared.** Two mutable views of one list conflict even
  when their bounds do not overlap, since the bounds are run-time values.
  A split into two views waits for a std method with the proof inside it.
- **A `mut [T]` parameter keeps read-only elements** (E0315): `mut` on a
  view does not make its elements writable; `&mut [T]` does.
- **There is no `&mut str`** (E0901); the spec example now returns
  `&mut List<str>`.
- **Slice patterns stay deferred**, now as their own item rather than
  waiting on slice borrows: `[first, ..rest]` would bind `rest` as a view,
  which the rules above already describe, but no compiler source or test
  needs the syntax yet.

## Consequences

- Programs that mutated through a reference while a reborrow was live, or
  grew a list while a range view was used, are now rejected (E0700, E0701,
  E0703, E0704). The compiler sources needed no change.
- Stage 1 only: stage 0 has no `&mut [T]` and no borrow checker. Stage 0
  and stage 1 give `&xs[a..b]` the same type, `&Slice<T>`.
- The fixtures pair each accepted shape with a rejected one:
  `tests/borrow/reborrows*.of` and `tests/borrow/sub_slices*.of`, next to
  the existing field and escape fixtures.
