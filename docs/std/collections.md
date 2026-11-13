# `std.collections`: `List`, `Map`, `Box`

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
bounds. `keys()` and `values()` wait for iterators; `for key, value in
map` visits the entries.

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
| `fn keys(self) MapKeys<K, V>`, `values()` | borrowing iterators | |

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
