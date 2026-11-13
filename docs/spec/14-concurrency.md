# 14. Concurrency

Concurrency is not part of the bootstrap language: tasks (14.2), the
marker interfaces (14.3), channels and the `std.sync` primitives (14.4),
and cancellation (14.5) are implemented by stage 1 only; `std.time`
compiles with both stages.

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

`run` returns a `task.Task<T>`. Dropping a task without a join detaches
it: the task runs on, and its result is dropped when it ends. A task may
start and join tasks of its own. A task that panics ends the whole program,
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

`boundedChannel<T: Send>(capacity)` returns a channel that holds at most
`capacity` values: `send` waits while it is full. Once the receiver is
gone, `send` drops the value without waiting. A capacity below 1 panics.

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
  references; these operations are sequentially consistent. Each has a
  `...With` form (`loadWith`, `storeWith`, `fetchAddWith`, `swapWith`,
  `compareExchangeWith`) that takes a `std.mem.atomic.Ordering` as its
  last argument, at run time; an ordering the operation does not allow is
  `SeqCst`. Both types are built on the intrinsics below.
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
- `WaitGroup` counts work in progress: `add(n)`, `done()` (`add(-1)`) and
  `wait()`, which waits until the count is 0. A count below 0 panics.

## 14.5 Cancellation, deadlines and shutdown

```ofir
import std.sync.CancelToken
import std.sync.Recv
import std.sync.channel
import std.task
import std.time
import std.time.Deadline

fn main() {
    const (tx, rx) = channel<int>()
    const stop = CancelToken.new()
    const mine = stop.clone()
    const worker = task.run(move () int => {
        let sum = 0
        for !mine.isCancelled() {
            match rx.recvUntil(Deadline.after(time.MILLISECOND)) {
                Recv.Value(v) => {
                    sum += v
                },
                Recv.TimedOut => {},
                Recv.Closed => {
                    mine.cancel()
                },
            }
        }
        return sum
    })
    tx.send(42)
    stop.cancel()
    println("{worker.join() <= 42}")
}
```

`std.time` reads a monotonic clock in `int` nanoseconds (`now`, with the
constants `MICROSECOND`, `MILLISECOND` and `SECOND`) and sleeps
(`sleep(ns)`). A `Deadline` is a point on that clock: `Deadline.after(ns)`,
`Deadline.never()`, `expired()` and `remaining()`.

The waits of `std.sync` have a form that takes a deadline:
`Receiver.recvUntil` returns a `Recv<T>`, which is `Value(value)`,
`TimedOut` or `Closed`; `WaitGroup.waitUntil` and `CancelToken.waitUntil`
return whether the wait ended before the deadline. A deadline that has
passed makes them check once without waiting.

Cancellation is cooperative. A `CancelToken` is shared by its clones:
`cancel` sets it once and wakes its waiters, and `isCancelled`, `wait` and
`waitUntil` observe it; writes made before `cancel` are visible to a task
that sees the token cancelled. A task stops by returning, so it drops what
it owns, and its join returns. Nothing forces a task to stop from outside,
since that would skip its drops (14.1): a task that never checks its token
runs to its end, and a program ends when `main` returns, whatever its
detached tasks are doing.

A graceful shutdown drops the senders, so that the workers drain their
queues and see `Closed`, then waits for them with
`WaitGroup.waitUntil(deadline)`; a hurried one cancels a token first. When
a worker ends, its receiver drops the values still queued, and a sender
waiting on its full bounded channel is released, dropping its value.

