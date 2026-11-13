# 0055. Sets, deques, heaps and sorted maps in `std.collections`

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-40 asks for sets, deques, priority queues and ordered collections,
chosen by workload, with their complexity, invalidation and allocation
contracts tested. `std.core` has only `List<T>` and `Map<K, V>`. Programs
that need a queue pop from the front of a list in O(n). A set is a
`Map<T, unit>`. A priority queue is a list that gets sorted again after
each push. Iteration in key order means sorting the keys of a map.

## Decision

1. A new package, `std.collections`, holds one module per collection:
   - `deque`: `Deque<T>`, a ring buffer;
   - `set`: `Set<T: Eq + Hash>`;
   - `heap`: `Heap<T: Ord>`;
   - `sorted`: `SortedMap<K: Ord, V>`.

   `std.core` stays small: these types are imported where they are used,
   and the prelude does not change.
2. **`Deque<T>`** is a ring buffer over one allocation (`ptr`, `cap`,
   `head`, `len`). The capacity is 0 or a power of two, and it doubles when
   full.
   - `pushBack`, `pushFront`, `popBack` and `popFront` take amortized O(1).
   - `get` and `d[i]` take O(1).
   - Growing moves the elements and invalidates every borrow, as for `List`.
3. **`Set<T>`** wraps `Map<T, unit>`. It iterates in insertion order and
   inherits the map's deterministic hashing. `insert`, `contains` and
   `remove` cost the same as the map's operations.
4. **`Heap<T>`** is a binary max-heap in a `List<T>`.
   - `push` and `pop` take O(log n); `peek` takes O(1).
   - A min-heap stores `Reverse<T>`, a wrapper that inverts `Ord`.
   - `intoSorted` returns the elements in ascending order in O(n log n).
5. **`SortedMap<K, V>`** keeps its keys sorted in one list and its values in
   a parallel list.
   - `get` is a binary search, O(log n).
   - `insert` and `remove` take O(n), because they shift the elements after
     the position.
   - Iteration is in key order, and `range(lo, hi)` gives the entries with
     keys from `lo` up to but not including `hi`.

   This beats a tree for the small and read-mostly maps that programs
   build. A B-tree can replace the layout later behind the same API, if a
   benchmark asks for it.
6. A test asserts each contract:
   - that the results come in the documented order;
   - that capacity is reused, so no allocation happens below the capacity;
   - that every element is dropped exactly once, including on `clear`,
     on `drop` and when a partly consumed iterator is dropped.

## Consequences

- Programs pick a collection by workload, and `docs/std/collections.md`
  lists the costs side by side.
- `SortedMap` insertion is O(n). The docs say so, and point to building
  the map from a sorted list for a bulk load.
- `Reverse<T>` is the only adapter for the order of a heap. A heap ordered
  by a key function waits for closures that can be stored in a struct.
