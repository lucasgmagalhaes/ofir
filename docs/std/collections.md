# `std.collections`: `List`, `Map`, `Box` and the collections package

These types are in the prelude.

## `List<T>`

A growable array. Literals: `[1, 2, 3]`, `[0; 16]` (16 zeros), `[]` (type
from context).

| Method | Description | S0 |
|--------|-------------|:--:|
| `fn new() List<T>` | empty, no allocation | ✓ |
| `fn withCapacity(n: int) List<T>` | allocates | ✓ |
| `fn len(self) int` | | ✓ |
| `fn isEmpty(self) bool` | | ✓ |
| `fn push(mut self, x: move T)` | may allocate | ✓ |
| `fn pop(mut self) Option<T>` | | ✓ |
| `fn insert(mut self, i: int, x: move T)` | **panics** if `i > len` | ✓ |
| `fn remove(mut self, i: int) T` | shifts later elements; **panics** if out of range | ✓ |
| `fn swapRemove(mut self, i: int) T` | O(1), does not preserve order | ✓ |
| `fn clear(mut self)` | drops all elements | ✓ |
| `fn truncate(mut self, n: int)` | | ✓ |
| `fn extend(mut self, other: move List<T>)` | moves the elements of `other` | ✓ |
| `fn clone(self) List<T>` | clones each element; allocates | ✓ |
| `fn get(self, i: int) Option<&T>` | returns a borrow (spec 5.6) | ✓ |
| `fn getMut(mut self, i: int) Option<&mut T>` | | ✓ |
| `fn first(self) Option<&T>`, `last()` | | ✓ |
| `fn contains(self, x: T) bool where T: Eq` | | ✓ |
| `fn sort(mut self) where T: Ord` | stable; S0 only for integers, `str` and tuples of those | ✓ |
| `fn sortBy(mut self, cmp: fn(T, T) Ordering)` | stable | |
| `fn reverse(mut self)` | | ✓ |
| `fn swap(mut self, i: int, j: int)` | O(1); **panics** if out of range | ✓ |
| `fn iter(self) ListIter<T>` | borrowing iterator: `ListIter<T>` is a `ref struct` (spec 5.6) | ✓ |
| `fn map<U>(self, f: fn(T) U) List<U>` | allocates | |
| `fn filter(self, f: fn(T) bool) List<T> where T: Clone` | allocates | |
| `fn join(self, sep: str) str where T: Display` | allocates | ✓ |

Indexing `l[i]` and slicing `l[a..b]` are bounds checked (spec 4.9).

Stage 0: `List` is written in `std/core/list.of`, marked `@lang("list")`;
the compiler builds literals with its `withCapacity`, `push`, `new` and
`filled`, and reaches elements through its `ptr` and `len` fields.
`for x in list` copies each element, so the element type must be Copy
(E0321): `for x in list.iter()` borrows each element instead; `sort` accepts int, float and str
elements until bounds.
A `[T]` parameter accepts a `List<T>` (spec 5.3); slice methods (`len`, `get`, `first`, `last`,
`contains`, `iter`) are shared. The slice method `fn toList(self) List<T> where T: Clone`
copies a slice into a new list (S0 for Copy element types).

## `Map<K, V>`

A hash map iterating in insertion order (spec 2.6). Requires `K: Eq + Hash`.

Stage 0: `Map` is written in `std/core/map.of` (a list of entries in
insertion order and an open-addressing table), and keys are integers,
bool, str or tuples of them (E0313), hashed by `std/core/hash.of` until
bounds. `for key, value in map` visits the entries; `keys()` visits the
keys alone.

| Method | Description | S0 |
|--------|-------------|:--:|
| `fn new() Map<K, V>` | | ✓ |
| `fn seeded() Map<K, V>` | hash keyed by a random seed (ADR 0034) | ✓ |
| `fn withSeed(seed: u64) Map<K, V>` | hash keyed by `seed` | ✓ |
| `fn len(self) int` | | ✓ |
| `fn insert(mut self, k: move K, v: move V) Option<V>` | returns the previous value | ✓ |
| `fn get(self, k: K) Option<&V>` | | ✓ |
| `fn getMut(mut self, k: K) Option<&mut V>` | | ✓ |
| `fn contains(self, k: K) bool` | | ✓ |
| `fn remove(mut self, k: K) Option<V>` | | ✓ |
| `fn getOrInsert(mut self, k: move K, default: move V) &mut V` | | ✓ |
| `fn keys(self) MapKeys<K, V>` | borrowing iterator over the keys, in insertion order | ✓ |

`for k, v in map` iterates entries (spec 4.4). With `K = str`, lookups
accept string slices and literals: `ages.get("ana")`.

## `Box<T>`

An owned heap allocation.

| Method | Description | S0 |
|--------|-------------|:--:|
| `fn new(x: move T) Box<T>` | allocates | ✓ |
| `fn unbox(move self) T` | moves the value out and frees the box | ✓ |
| `fn clone(self) Box<T>` | a new box with a clone of the value; allocates | ✓ |

`Box<T>` dereferences automatically for field access and method calls;
`*b` reads the value. A method of `Box` itself (`unbox`) takes precedence
over a method of the same name of the value; `(*b).m()` calls the latter.

Stage 0: `Box` is declared in `std/core/box.of`, marked `@lang("box")` so
that the compiler knows which type to dereference. Dropping a box drops
its value and frees the memory.

## `std.arena`: `Arena<T>`, `Id<T>`

Values of one type that are freed all together (ADR 0049). An `Id<T>` is a
`copy` handle, so values can link to each other, cycles included, without
borrowing the arena.

| Item | Description | S0 |
|------|-------------|:--:|
| `fn Arena.new() Arena<T>` | an empty arena | ✓ |
| `fn Arena.withCapacity(n: int) Arena<T>` | room for `n` values before it grows | ✓ |
| `fn alloc(mut self, value: move T) Id<T>` | moves a value in; ids count from 0 | ✓ |
| `fn get(self, id: Id<T>) &T` | the value of an id | ✓ |
| `fn getMut(mut self, id: Id<T>) &mut T` | the value of an id, to change it | ✓ |
| `fn len(self) int` | how many values it holds | ✓ |
| `fn clear(mut self)` | drops every value, keeps the storage | ✓ |
| `fn Id.index(self) int` | the position of the value, in allocation order | ✓ |

`get` and `getMut` panic for an id made before the arena's last `clear`.
There is no per-value free: an arena grows until `clear`.

## Choosing a collection

The modules of the `std.collections` package are imported where they are
used, as in `import std.collections.deque.Deque` (ADR 0055). Every
collection drops each element exactly once: when it is popped or removed
and goes out of scope, on `clear`, or with the collection. Growing moves
the elements, so a borrow of an element never outlives a push.

| Workload | Collection | Costs |
|----------|------------|-------|
| a stack, an indexed sequence | `List<T>` | push and pop at the end O(1), insert and remove O(n) |
| a queue, a sliding window | `Deque<T>` | push and pop at either end O(1), `get` O(1) |
| lookup by key | `Map<K, V>` | insert and get O(1) on average, remove O(n) |
| membership, deduplication | `Set<T>` | insert and contains O(1) on average, remove O(n) |
| the greatest or least element next | `Heap<T>` | push and pop O(log n), peek O(1) |
| keys in order, ranges of keys | `SortedMap<K, V>` | get O(log n), insert and remove O(n) |

## `Deque<T>` (`std.collections.deque`)

A ring buffer. The capacity is 0 or a power of two and doubles when full;
popping never frees memory, and `clear` keeps the buffer.

| Method | Description | S0 |
|--------|-------------|:--:|
| `fn new() Deque<T>` | empty, no allocation | ✓ |
| `fn withCapacity(n: int) Deque<T>` | allocates | ✓ |
| `fn len(self) int`, `isEmpty()`, `capacity()` | | ✓ |
| `fn reserve(mut self, extra: int)` | one reallocation | ✓ |
| `fn pushBack(mut self, x: move T)`, `pushFront` | may allocate | ✓ |
| `fn popBack(mut self) Option<T>`, `popFront` | | ✓ |
| `fn get(self, i: int) Option<&T>`, `getMut` | counted from the front | ✓ |
| `fn front(self) Option<&T>`, `back()` | | ✓ |
| `fn clear(mut self)` | drops all elements, keeps the capacity | ✓ |
| `fn iter(self) DequeIter<T>` | borrowing iterator, front to back | ✓ |

## `Set<T>` (`std.collections.set`)

A `Map<T, ()>`: requires `T: Eq + Hash` and iterates in insertion order.

| Method | Description | S0 |
|--------|-------------|:--:|
| `fn new() Set<T>`, `seeded()` | as `Map.new` and `Map.seeded` | ✓ |
| `fn len(self) int`, `isEmpty()` | | ✓ |
| `fn insert(mut self, x: move T) bool` | false when an equal element was there | ✓ |
| `fn contains(self, x: T) bool` | | ✓ |
| `fn remove(mut self, x: T) bool` | true when it was there | ✓ |
| `fn isSubset(self, other: &Set<T>) bool` | | ✓ |
| `fn clear(mut self)` | | ✓ |
| `fn iter(self) MapKeys<T, ()>` | borrowing iterator, insertion order | ✓ |

## `Heap<T>` and `Reverse<T>` (`std.collections.heap`)

A binary max-heap in a `List<T>`; requires `T: Ord`. Equal elements come
out in no particular order. `Heap<Reverse<T>>` pops the least element
first.

| Item | Description | S0 |
|------|-------------|:--:|
| `fn new() Heap<T>` | | ✓ |
| `fn fromList(items: move List<T>) Heap<T>` | O(n) | ✓ |
| `fn len(self) int`, `isEmpty()` | | ✓ |
| `fn push(mut self, x: move T)` | O(log n) | ✓ |
| `fn pop(mut self) Option<T>` | the greatest element, O(log n) | ✓ |
| `fn peek(self) Option<&T>` | O(1) | ✓ |
| `fn clear(mut self)` | keeps the capacity | ✓ |
| `fn intoSorted(move self) List<T>` | ascending, O(n log n) | ✓ |
| `struct Reverse<T> { pub value: T }` | `cmp` reversed | |

## `SortedMap<K, V>` (`std.collections.sorted`)

Sorted keys in one list and their values in a parallel list; requires
`K: Ord`. Reads are binary searches; an insertion or removal shifts the
later entries, so a bulk load inserts in key order.

| Method | Description | S0 |
|--------|-------------|:--:|
| `fn new() SortedMap<K, V>` | | ✓ |
| `fn len(self) int`, `isEmpty()` | | ✓ |
| `fn insert(mut self, k: move K, v: move V) Option<V>` | O(n); returns the previous value | ✓ |
| `fn get(self, k: K) Option<&V>`, `getMut` | O(log n) | ✓ |
| `fn contains(self, k: K) bool` | O(log n) | ✓ |
| `fn remove(mut self, k: K) Option<V>` | O(n) | ✓ |
| `fn lowerBound(self, k: K) int` | the first position whose key is not less than `k` | ✓ |
| `fn keys(self) &[K]`, `values()` | slices in key order | ✓ |
| `fn clear(mut self)` | | ✓ |
