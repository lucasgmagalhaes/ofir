# 0042. Atomic intrinsics, memory orderings and thread-local globals

- Status: Accepted
- Date: 2026-11-13

## Context

The allocator pool (`runtime/src/pool.c`, ADR 0041), tasks and channels
are still C, because Ofir lacks three things they need:

- **Thread-local storage.** The pool keeps a free list per thread
  (`_Thread_local Bin bins[...]`). Ofir has mutable globals (spec 11.5),
  but one copy is shared by every thread.
- **Atomics with orderings.** `AtomicInt` and `AtomicBool` call
  `ofir_atomic_*` in `runtime/src/sync.c`, and every operation is
  sequentially consistent. A call to C cannot be inlined, so `Arc.clone`
  costs a call where one `lock xadd` would do. A spinlock or a counter
  needs only acquire, release or relaxed ordering.
- **Globals beyond numbers.** Stage 1 stores mutable globals of numeric
  and `bool` types only (E0901), and stage 0 parses them but cannot read
  or write them. The std is compiled by stage 0, so the std cannot use one
  until stage 0 can.

## Decision

- **Orderings.** `std.sync.Ordering` is an enum: `Relaxed`, `Acquire`,
  `Release`, `AcqRel`, `SeqCst`, with the meaning of the C11 orderings.
- **Atomic intrinsics.** `std.sync` declares unsafe functions that the
  compiler lowers to LLVM instructions instead of calls:
  `atomicLoad`, `atomicStore`, `atomicSwap`, `atomicFetchAdd`,
  `atomicFetchSub`, `atomicCompareExchange` and `fence`, on a `*mut T`
  where `T` is an integer, `bool` or a raw pointer. The ordering argument
  must be a constant (E0820). An ordering that the operation does not
  allow, such as `Release` for a load, is E0821.
- **`AtomicInt` and `AtomicBool`** keep their API and their sequentially
  consistent default. Each method gains an ordering form (`loadWith`,
  `storeWith`, `fetchAddWith`, ...). `ofir_atomic_*` leave the runtime.
- **Thread-local globals.** `@threadLocal let NAME: T = constant` declares
  a mutable global with one copy per thread, made from the initializer
  when the thread starts. Access requires `unsafe`, as for any mutable
  global (E0803). `T` must have no drop (E0822), because a thread ends
  without running drops of its globals. `@threadLocal` on a `const` or on
  anything but a module-level `let` is E0808.
- **Global types.** Mutable globals, thread-local or not, may have any type
  whose constant initializer the compiler can lay out: numbers, `bool`,
  raw pointers (initialized to `null`), fixed arrays and `@repr(C)`
  structs of those. Stage 0 implements reads, writes and borrows of them,
  so the std can use them.
- **Order of work.** Stage 0 first (the std depends on it), then stage 1,
  then `AtomicInt`/`AtomicBool` on the intrinsics, then the pool in Ofir
  (`std/core/pool.of`) behind `ofir_alloc`, with `@export` (spec 12).
  Each step must keep `make bench-compare` within 3%.

## Consequences

- An atomic is one instruction that LLVM can inline and combine;
  `Arc` and `WaitGroup` stop paying for a call.
- The pool, and later tasks and channels, can move to Ofir. Only the code
  that talks to the OS (`mmap`, `pthread_create`) stays an `extern`.
- Getting an ordering wrong is a data race, so the intrinsics are unsafe;
  the safe types keep sequential consistency unless asked otherwise.
- A thread-local needs no destructor at thread exit, which keeps the
  runtime simple; code that must give resources back at exit (as the pool
  does) calls a function for it, as `ofir_pool_thread_exit` does today.
- Globals of `str`, `List` or other owning types stay E0901: their
  initializer allocates, and no code would drop them.
