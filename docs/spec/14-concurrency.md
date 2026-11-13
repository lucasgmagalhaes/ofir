# 14. Concurrency

Concurrency is not part of the bootstrap language: tasks (14.2), the
marker interfaces (14.3), channels and the `std.sync` primitives (14.4) are
implemented by stage 1 only.

## 14.1 Goals

- **No data races in safe code**, enforced at compile time, as with Rust.
- **No mandatory runtime**: programs that do not use concurrency pay
  nothing for it.
- **Simple model first**: tasks that communicate through channels, each
  task on an OS thread of its own. A later M:N scheduler makes tasks
  lightweight behind the same `task.run` API.
- **No `async`/`await`** (ADR 0027): functions have no color; the same
  function runs in a task or outside one.

## 14.2 Tasks

```ofir
import std.task

fn main() {
    const data = [1, 2, 3]
    const handle = task.run(move () int => {
        let sum = 0
        for x in data {
            sum += x
        }
        return sum
    })
    println("{handle.join()}")
}
```

`task.run` starts a task and returns its handle; `join` waits for the task
and returns its result. A task runs on an OS thread of its own. `task.run`
takes a `move fn() T + Send` (chapter 10.2, 14.3), so a task can never hold
a reference into another task's stack, nor a value that is unsafe to use
from another thread.

`run` returns a `task.Task<T>`. A task that panics ends the whole program,
as a panic on the main thread does. The output of `print` and `println`
reaches standard output a line at a time, so lines from different tasks
never interleave.

## 14.3 Marker interfaces

Two built-in marker interfaces (no methods), derived from the structure of
a type:

| Interface | Meaning |
|-----------|---------|
| `Send` | values of the type may be moved to another task |
| `Sync` | shared references to the type may be used from several tasks |

- Numbers, `bool`, `str`, `()`, `never` and key types are both.
- Raw pointers (`*T`, `*mut T`) and `dyn I` are neither.
- `&T` is `Send` when `T` is `Sync`; `&mut T` is `Send` when `T` is `Send`.
  Both are `Sync` when `T` is `Sync`.
- A slice, array or tuple is `Send` (`Sync`) when its elements are.
- A struct or enum is `Send` (`Sync`) when all its fields are; a recursive
  type counts as `Send` (`Sync`) where it reaches itself again.
- A struct or enum marked `@threadsafe` (chapter 12) is `Send` (`Sync`)
  when its type arguments are, whatever its fields hold. The marker is how
  a type built on raw pointers, such as `List<T>`, `Box<T>` or `str`,
  states that it owns what they reach. The compiler cannot check it, so it
  belongs to code that is correct under `unsafe` rules (chapter 11).
- `UnsafeCell<T>` (chapter 13), and so `Cell<T>`, is never `Sync`: its
  value changes through shared references. It is `Send` when `T` is.
- A type parameter is `Send` (`Sync`) when it has that bound:
  `fn spawn<T: Send>(x: move T)`.

A function type may add `+ Send`: `move fn() T + Send`. A closure
converts to it only if it may run on another task: a `move` closure moves
its captures, which must all be `Send`; a borrowing closure shares them,
which must all be `Sync` (E0360). A named function captures nothing and
converts to any `+ Send` type. `fn() T + Send` converts to `fn() T`, and
no function type is `Sync`.

```ofir
import std.task

fn twice(f: fn() int + Send) int {
    return f() + f()
}

fn main() {
    const n = 21
    println("{twice(() int => n)}")
    const t = task.run(move () int => n * 2)
    println("{t.join()}")
}
```

`task.run` takes a `move fn() T + Send` and requires `T: Send`, since the
result moves back to the task that calls `join`. Stage 0 has no tasks: it
accepts `@threadsafe` and does not check `Send`.

## 14.4 Communication

```ofir
import std.sync.channel
import std.task

fn main() {
    const (tx, rx) = channel<int>()
    const t = task.run(move () => tx.send(42))
    println("{rx.recv().unwrap()}")
    t.join()
}
```

`channel<T: Send>()` in `std.sync` returns a `Sender<T>` and a
`Receiver<T>`. The channel is unbounded: `send` never waits, and it drops
the value if the receiver is gone. `clone` on a sender makes another
sender of the same channel. `recv` waits for the next value and returns
`None` once the queue is empty and every sender is dropped; `tryRecv`
returns `None` instead of waiting. The values of one sender arrive in the
order it sent them. Dropping the receiver drops the values it never took.

The synchronization primitives of `std.sync` are `Sync`, so an `Arc`
shares them between tasks:

```ofir
import std.sync.Arc
import std.sync.Mutex
import std.task

fn main() {
    const total = Arc.new(Mutex.new(0))
    const mine = total.clone()
    const t = task.run(move () => {
        let guard = mine.get().lock()
        *guard.getMut() += 1
    })
    t.join()
    println("{*total.get().lock().get()}")
}
```

- `Arc<T: Send + Sync>` owns a value shared between tasks. `clone` adds an
  owner, with an atomic count; the last owner to be dropped drops the
  value. `get` borrows it; `owners` returns the count.
- `Mutex<T>` holds its value: `lock` waits for the lock and returns a
  `MutexGuard<T>`, a `ref struct` whose `get` and `getMut` reach the value
  and whose drop unlocks. A task that locks a mutex it already holds waits
  forever.
- `RwLock<T>`: `read` returns a `ReadGuard<T>` (`get`), which other
  readers may hold at the same time; `write` returns a `WriteGuard<T>`
  (`get`, `getMut`), held alone.
- `AtomicInt` (`load`, `store`, `fetchAdd`, `swap`, `compareExchange`) and
  `AtomicBool` (`load`, `store`, `swap`) change their value through shared
  references; every operation is sequentially consistent.
- `std.mem.atomic` (ADR 0042) declares `Ordering` (`Relaxed`, `Acquire`,
  `Release`, `AcqRel`, `SeqCst`, as in C11) and the unsafe intrinsics
  `atomicLoad`, `atomicStore`, `atomicSwap`, `atomicFetchAdd`,
  `atomicFetchSub`, `atomicCompareExchange` and `fence`, which the
  compiler lowers to single instructions on a `*mut T` of an integer,
  `bool` or raw pointer. The ordering must be a constant `Ordering.X`
  (E0820) that the operation allows (E0821): a load takes `Relaxed`,
  `Acquire` or `SeqCst`; a store `Relaxed`, `Release` or `SeqCst`; a fence
  any ordering but `Relaxed`; the others any ordering.
  `atomicCompareExchange` returns the value it found, which equals the
  expected value when it stored the desired one.
- *Planned (ADR 0042):* `AtomicInt` and `AtomicBool` gain `...With` forms
  that take an ordering.
- `WaitGroup` counts work in progress: `add(n)`, `done()` (`add(-1)`) and
  `wait()`, which waits until the count is 0. A count below 0 panics.
