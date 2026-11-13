# 5. Ownership and borrowing

Ofir manages memory without a garbage collector. Every value has exactly one
**owner**; when the owner goes out of scope, the value is destroyed and its
memory is released.

Most code never deals with references explicitly. **Parameters are borrowed
by default**: a function reads its arguments without taking ownership and
without the caller writing `&`. Two keywords in the signature cover the other
cases: `mut` (the function modifies the argument) and `move` (the function
takes ownership). Call sites look the same in all three cases.

```ofir
fn total(xs: List<int>) int { ... }            // reads xs
fn reset(xs: mut List<int>) {   // modifies the caller's list
    xs.clear()
}
fn keep(xs: move List<int>) { ... }             // takes ownership

let nums = [1, 2, 3]
println("{total(nums)}")
reset(nums)
keep(nums)                                      // nums is moved here
```

## 5.1 Ownership

```ofir
fn main() {
    const a = "hello"           // a owns the string
    const b = a                 // ownership moves to b
    println(a)                // error: use of moved value `a`
}
```

- Assigning a move type to a variable or field, passing it to a `move`
  parameter, or returning it **moves** it. The source can no longer be used
  (unless it is a `let` that is assigned a new value).
- Copy types (chapter 2.9) are copied instead of moved.
- To duplicate a move type explicitly, call `.clone()`.
- A value can only be moved out of a place the function owns entirely.
  `const x = list[i]`, or moving a field out of a borrowed parameter, is an
  error; use `list.remove(i)`, `list.swapRemove(i)`, `mem.replace(list[i], v)`,
  `opt.take()` (on an `Option`) or `.clone()` instead.

Stage 0 reports E0600 (use of a moved value), E0601 (a variable that may
not be initialized) and E0602 (moving out of a borrowed, indexed or
referenced place, or a `mut` parameter left without a value).

### Partial and conditional moves

```ofir
let v = makeList()
if flag {
    consume(v)          // fn consume(xs: move List<int>)
}
println("{v.len()}")    // error: `v` may have been moved
```

A value that is moved on some paths is considered moved after the branches
join. Moving a field out of a struct is allowed only if the struct is not
used as a whole afterwards (except to reassign the moved field).

## 5.2 Destruction (drop)

When a binding goes out of scope, its value is dropped unless it was moved.
Values are dropped in reverse order of declaration. Dropping a value:

1. calls its `drop` method, if it has one;
2. then drops each field, in declaration order.

Temporaries are dropped at the end of the statement that created them
(of a condition, for the condition of an `if` or a loop). `const _ = value`
drops the value at once. A struct or enum with a `drop` method is dropped
whole: moving a field out of it is an error (E0602).

If a value is moved on some paths only, the compiler inserts a hidden drop
flag so it is dropped exactly once. This is the only runtime cost of the
model.

### Cloning

`x.clone()` returns an independent copy of `x`, which it only reads. A
type that declares `fn clone(self) Self` is cloned by that method; `str`,
`List<T>` and `Box<T>` declare one in std. Every other struct, enum and
tuple gets a derived clone, which clones each field (of the active variant,
for an enum), when all its parts can be cloned: Copy types are copied, and
a type with a `drop` method but no `clone` method cannot be cloned, since
the resource it owns has no known copy (E0322). A Copy value's clone is
the value itself.

`.clone()` on a value of a type parameter `T` needs the bound `T: Clone`
(chapter 8.1); without it, it is an error (E0322). Stage 0 has no bounds:
std reaches the clone of any `T` with the `cloneOf<T>` intrinsic.

## 5.3 Parameter modes

| Declaration | Mode | The function may | The caller passes |
|-------------|------|------------------|-------------------|
| `x: T` | **borrow** (default) | read `x` | any expression; it keeps ownership |
| `x: mut T` | **mutable borrow** | read, modify and reassign `x` | a mutable place (a `let`, a field or element of one, or a `mut` parameter) |
| `x: move T` | **move** | own `x`: store it, return it, drop it | a value, which is moved (or copied, for Copy types) |

The `self` parameter of a method (chapter 3.6) uses the same modes:

```ofir
struct Point {
    x: float
    y: float

    fn length(self) float { ... }           // borrows the point
    fn scale(mut self, k: float) { ... }    // modifies it
    fn intoVec2(move self) Vec2 { ... }     // consumes it
}
```

### Borrowed parameters

- A borrowed parameter is read-only: assigning to it, mutating it, or moving
  out of it is an error. To work on a local copy, write `let x = x.clone()`
  (or `let n = n` for Copy types).
- Any expression can be passed: a variable, a field, a literal or the result
  of a call. A temporary lives until the end of the statement, so
  `println(user.describe())` needs nothing special.
- The compiler chooses how to pass the argument: small Copy values in
  registers, everything else by pointer. Since the callee cannot modify it,
  the choice is invisible to the program.

### Mutable parameters

- The caller's place must be mutable: passing a `const` binding or a borrowed
  parameter to a `mut` parameter is an error. Nothing is written at the call
  site: `reset(nums)`.
- Assigning a new value to the whole parameter (`x = T{...}`) replaces the
  caller's value; the old value is dropped.
- Moving out of a `mut` parameter is allowed only if a new value is assigned
  before the function returns (or `mem.replace`/`Option.take` is used).

### Moved parameters

- The callee owns the value: it can store it in a struct or collection,
  return it, or let it be dropped at the end of the function.
- Inside the function, a `move` parameter is an immutable binding; rebind it
  with `let x = x` to modify it.
- If a function tries to store a borrowed parameter, the error suggests the
  fix: *"declare `item: move str`, or call `item.clone()`"*.

### Borrowed views of sequences and text

As borrowed or `mut` parameter types, `[T]` and `str` accept any contiguous
sequence or text:

```ofir
fn sum(xs: [int]) int { ... }       // accepts List<int>, [int; N] and slices
fn shout(s: str) str { ... }        // accepts str values, literals and slices

sum(numbers[0..10])
shout("hello")
```

The elements of a `[T]` are read-only, `mut` parameter or not: a function
that writes them takes a `&mut [T]` (section 5.6).

A borrowed `dyn I` parameter accepts any value whose type satisfies `I`
(chapter 9.7): `fn draw(s: dyn Shape)`.

## 5.4 Borrowing rules

While a call runs, its borrowed arguments are **borrows** of the caller's
places: shared borrows for the default mode and exclusive borrows for `mut`.
For each value, at any point:

- there may be **any number of shared borrows**, **or**
- **exactly one exclusive borrow**,

but never both. While a value is borrowed, it cannot be moved, and while it
is exclusively borrowed it cannot be read or written through its owner.

```ofir
fn append(dst: mut List<int>, src: List<int>) { ... }

append(a, b)       // ok
append(a, a)       // error: `a` is borrowed mutably and shared at the same time
```

**Borrows are tracked per place**, not per variable: distinct fields of a
struct (and distinct elements of a tuple) can be borrowed independently.
Indexing (`list[i]`) borrows the whole collection.

```ofir
lx.interpolations.push(lx.brackets.len())   // ok: two different fields of lx
```

**Two-phase borrows.** In a call, the arguments are evaluated before an
exclusive borrow of `mut self` or of a `mut` argument becomes active, so
`list.push(list.len())` is accepted.

Because `mut` arguments never alias anything else, the compiler tells LLVM
they are `noalias`, enabling optimizations that C compilers cannot perform
safely.

## 5.5 Borrowed bindings

The same borrow modes apply to other bindings that refer to an existing
value instead of owning it:

| Binding | Mode |
|---------|------|
| `for x in list` | borrows each element (Copy elements are copied) |
| `for mut x in list` | borrows each element mutably; `list` must be a mutable place |
| `match` / `if const` on a place that is not owned (a borrowed parameter, a field of one, an element) | bindings borrow the matched parts (Copy parts are copied) |
| `match` on an owned value | bindings take ownership of the parts |

```ofir
for mut item in cart.items {
    item.price = item.price * 90 / 100
}
```

## 5.6 Explicit references (advanced)

The types `&T` and `&mut T` still exist for the cases parameter modes do not
cover. They are **second-class**: a reference may be bound to a local
(`const r = &list`), returned from a function under the rule below, or stored
in a `ref struct`, but never stored in an ordinary struct, enum or
container. Because a reference can never outlive the call or block that
created it, no lifetime annotations are ever needed.

```ofir
let list = [1, 2, 3]
const r = &list
println("{r.len()}")   // last use of r: the borrow ends here
list.push(4)           // ok
```

A borrow lasts from the point where it is created to its **last use**
(non-lexical borrows).

**Stage 0** checks where references may be stored and returned (E0702),
and that writes go through `&mut`, but does not check borrows against
moves and mutations of what they borrow: that is the borrow checker of the
self-hosted compiler (roadmap 4.10; E0700, E0701, E0703 to E0705). The
self-hosted compiler also checks that a borrow does not outlive the block
of the value it borrows (E0706):

```ofir
let r = &outer
{
    const inner = [1, 2, 3]
    r = &inner          // error: `inner` does not live long enough
}
println("{r.len()}")
```

A function that returns a borrow receives its source by address, so that
the borrow points into the caller's value.

### Views of part of a sequence

`&xs[a..b]` is a `&[T]` over the elements `a` to `b - 1` of a list or
slice, and `&mut xs[a..b]` a `&mut [T]` over them, through which elements
are written (`xs[a..]` and `xs[..b]` too). A mutable view needs a mutable
list, or another `&mut [T]`; a `&[T]` has no mutable views. Either view
borrows `xs` until its last use, like `&xs` and `&mut xs`:

```ofir
fn fill(xs: &mut [int], v: int) { ... }

let a = [1, 2, 3, 4]
fill(&mut a[1..3], 9)    // a is [1, 9, 9, 4]
const tail = &mut a[2..]
a.push(5)                // error: `a` is borrowed by `tail`
tail[0] = 0
```

Two mutable views of the same list conflict even when their ranges do not
overlap: the compiler does not compare the bounds, which are known only at
run time. The checks are made by the self-hosted compiler; stage 0 has no
`&mut [T]` (E0901).

### Reborrows

A borrow of a place reached through a reference (`&mut *r`, `&r.field`,
`&s[i]` for a slice `s`) is a **reborrow**: it borrows the reference `r`
itself, as well as what `r` borrows. While a mutable reborrow is used, `r`
cannot be used; while a shared one is used, `r` can be read but nothing can
be written or borrowed mutably through it. When the reborrow is no longer
used, `r` is usable again:

```ofir
let list = [1, 2]
const r = &mut list
const inner = &mut *r
inner.push(3)
r.push(4)            // ok: `inner` is not used after this point
const view = &*r
r.push(5)            // error: `r` is borrowed as shared by `view`
println("{view.len()}")
```

Passing a reference to a function reborrows it for the call, so
`bump(r); bump(r)` is accepted.

### Returning a borrow

A function may return a reference (or a `ref struct`, or an `Option`/`Result`
containing one) that borrows from **one** parameter:

- from `self`, if the function is a method taking `self` or `mut self`;
- otherwise from its only borrowed parameter of a non-Copy type.

With no such parameter, or with several candidates, returning a borrow is an
error.

```ofir
// in List<T>:  fn first(self) Option<&T> { ... }
// in User:     fn tagsMut(mut self) &mut List<str> { ... }

const f = list.first()     // f borrows `list`
list.push(4)             // error: `list` is borrowed by `f`
```

The caller treats the result like any other local reference: it can be used,
passed to functions, and returned further only under the same rule.

There is no `&mut str` (E0901): text is changed through the value that
owns it, such as a `mut` parameter or a `&mut` to the struct holding it.

### `ref struct`

A struct declared with `ref` may contain references. It is itself
second-class: it can be bound locally, passed as an argument and returned
under the rule above, but not stored in ordinary structs or containers.

```ofir
ref struct ListIter<T> {
    list: &List<T>
    index: int

    fn next(mut self) Option<&T> {
        ...
    }
}

// in List<T>:
fn iter(self) ListIter<T> {
    return ListIter{list: &self, index: 0}
}
```

This is how iterators, string splitters and parsers over borrowed text are
written.

## 5.7 Why this is fast

- No garbage collector and no reference counting: allocation and release
  points are known at compile time.
- Borrowed parameters are passed by pointer or by value as the compiler sees
  fit, never copied deeply.
- `mut` parameters and `&mut` references never alias, so they are marked
  `noalias` for LLVM; borrowed parameters of types without interior
  mutability are `noalias readonly`.

## 5.8 Interior mutability

Some types must allow mutation through a shared borrow: counters shared by
several owners, caches, locks. They are built on one primitive,
`UnsafeCell<T>` in `std.mem.cell`, the only way to obtain a `*mut T` from a
shared borrow without undefined behavior. Safe wrappers in the standard
library:

| Type | Use |
|------|-----|
| `Cell<T>` (T Copy) | `get` / `set` a value through a shared borrow |
| `Mutex<T>`, `RwLock<T>`, atomics | shared mutation across threads (chapter 14) |

A type **has interior mutability** if it contains an `UnsafeCell` directly or
through its fields, tuple elements, array elements or enum payloads; a
pointer or reference to it does not count. The compiler never marks borrows
of such types as `readonly` and passes them by pointer, so a `set` through a
borrowed parameter or a binding of a borrowed place is seen by the owner
(chapter 13.5). `RefCell` (borrow checking at runtime) is intentionally not
provided.

```ofir
import std.mem.cell.Cell

fn bump(counter: &Cell<int>) {
    counter.set(counter.get() + 1)
}
```

Stage 1 only: `Cell<T: Copy>` needs a bound, which stage 0 does not parse.
A borrowed parameter whose type is a type parameter instantiated with such a
type is still passed by value.

## 5.9 Shared ownership

A value with several owners is held through a reference-counted pointer.
These are library types built on the rules above, not language features:

| Type | Module | Counts | Crosses tasks |
|------|--------|--------|---------------|
| `Rc<T>`, `Weak<T>` | `std.mem.rc` | plain integers | no: neither `Send` nor `Sync` |
| `Arc<T>` (T `Send + Sync`) | `std.sync` | atomic | yes (chapter 14.4) |

- `Rc.new(value)` moves the value into one heap block with its counts.
  `clone` adds a strong owner; `get` borrows the value. Dropping the last
  strong owner drops the value.
- `rc.downgrade()` gives a `Weak<T>`, an owner that does not keep the value
  alive. `weak.upgrade()` gives `Option.Some(rc)` while a strong owner
  remains, and `Option.None` after. The heap block is freed when the last
  owner of either kind goes.
- `owners()` and `weakOwners()` read the counts; `ptrEq` compares the
  blocks.
- The value is shared, so it is reached only by `&T`. Mutation goes
  through interior mutability (5.8): a `Cell` or a lock inside `T`.
- Counting cannot see a cycle of strong owners: such a cycle is never
  dropped and its memory leaks. Leaking is safe but a bug; break cycles
  by making the back edges `Weak`, as a child points to its parent.

```ofir
import std.mem.rc.Rc

const a = Rc.new(5)
const b = a.clone()
const w = a.downgrade()
println("{a.owners()} {*b.get()}")   // 2 5
```

Stage 1 only: stage 0 cannot clone a generic struct through its `clone`
method. Arena allocators are planned.
