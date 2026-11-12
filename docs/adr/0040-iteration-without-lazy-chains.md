# 0040. Iteration contracts, without lazy adapter chains

- Status: Accepted
- Date: 2026-11-12

## Context

Roadmap item OFIR-11 asks to settle how loops borrow what they iterate,
how a loop consumes a collection, and whether std offers lazy adapter
chains such as `xs.iter().map(f).filter(g).collect()`.

Before this record, `for mut item in list` copied each element into
`item`: a change never reached the list, and assigning a field of an
element that owns memory freed it twice. There was no way to move the
elements out of a list in a loop other than `pop` in reverse order.

Lazy chains need an iterator type per adapter, closures stored in those
types, and either interfaces with associated types or a generic for each
step's item. Ofir has `move fn` closures and generic interfaces, but each
adapter would be a ref struct holding a closure and the iterator below
it, and every chain would monomorphize a new stack of them.

## Decision

- `for item in list` borrows the list for the whole loop, shared;
  `for mut item in list` borrows it mutably, and `item` is the element
  itself, so changes reach the list. The list must be mutable. For a map,
  `for mut key, value` gives the value in place.
- Consuming iteration is an iterator that owns its items:
  `List.drain()` moves the elements out first to last and leaves the
  list empty; elements a `break` leaves behind are dropped with the
  iterator.
- Std has no lazy adapter chains. A `for` loop over the elements writes
  the same computation. A user type may still define adapters as
  iterators.

## Consequences

- The borrow rules of spec 4.4 are checked by stage 1 (E0700, E0701,
  E0703, E0315); stage 0 lowers `for mut` the same way.
- Programs build results with loops (`for x in xs { if keep(x) { out.push(f(x)) } }`)
  instead of chains.
- A later record may add adapters if loops prove too verbose; that would
  be new std API, not a change to the loop contracts.
