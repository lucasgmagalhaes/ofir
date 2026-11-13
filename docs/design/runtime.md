# Runtime

The runtime is a small C library (`runtime/`) linked into every Ofir
program. It contains only what cannot be written in Ofir or in portable
LLVM IR. Its interface is `runtime/include/ofir_rt.h`.

## 1. Responsibilities

| Area | Functions |
|------|-----------|
| Entry | `int main(int argc, char **argv)` stores the arguments and calls the Ofir entry point `ofir_main` |
| Panics | `ofir_panic(msg, len, file, line, col)` and specialized cold functions for common checks |
| Memory | `ofir_alloc`, `ofir_realloc`, `ofir_free` (wrapping `malloc`/`realloc`/`free`, panicking on failure) |
| I/O | `ofir_write(fd, ptr, len)`, `ofir_read(fd, ptr, len)`, the standard output buffer (`ofir_stdout_write`, `ofir_stdout_flush`) |
| Crashes | stack overflow detection (section 3) |
| Process | `ofir_exit(code)`, `ofir_arg_count()`, `ofir_arg(i, &len)`, `ofir_env` |
| Files | thin wrappers over `open`, `read`, `write`, `stat`, `mkdir`, `unlink`, `opendir`, `getline` |
| Processes | `ofir_spawn`, `ofir_wait`: `posix_spawnp` (or `CreateProcess`) with both output streams sent to temporary files, so several clang processes can run at once (ADR 0033); `ofir_cpu_count` |
| Tasks | `ofir_task_spawn`, `ofir_task_join`: one POSIX thread per task (`std.task`, spec 14.2) |
| Channels | `ofir_channel_new`, `_send`, `_recv`, `_try_recv` and the sender and receiver counts (`std.sync`, spec 14.4) |
| Locks | `ofir_mutex_*`, `ofir_rwlock_*`, `ofir_waitgroup_*` (`std.sync`, spec 14.4); the atomics are intrinsics (`std.mem.atomic`, ADR 0042) |

Stage 0 implements the entry point, the panic functions, the allocator,
the standard output buffer and the string functions used by
interpolation (`runtime/src`). Values of type `str` are passed to the
runtime by pointer, which keeps the C ABI of generated calls simple.

The system functions (`os.c`, `fs.c`) serve std.os, std.fs and std.io,
which call them through `extern "C"` blocks: a string goes in as a
pointer and a length (the runtime adds the NUL that C wants, and rejects
a string that holds one with `EINVAL`), and a result comes back in an
`OfirBuf` the runtime allocates with `ofir_alloc`, which std takes over
as a `str` or a `List<u8>` without copying. They return 0 or an `errno`
value; `ofir_error_kind` and `ofir_error_message` turn it into an
`IoError`.

## 2. Panic functions

Generated code calls specialized functions so that the call sites stay
small and the messages are consistent:

```c
_Noreturn void ofir_panic(const uint8_t *msg, size_t len, const OfirLoc *loc);
_Noreturn void ofir_panic_bounds(size_t index, size_t len, const OfirLoc *loc);
_Noreturn void ofir_panic_overflow(const OfirLoc *loc);
_Noreturn void ofir_panic_div_zero(const OfirLoc *loc);
_Noreturn void ofir_panic_unwrap_none(const OfirLoc *loc);

typedef struct { const char *file; uint32_t line, col; } OfirLoc;
```

`OfirLoc` values are constant globals emitted by codegen. All panic
functions are `cold` and `noreturn`, so they do not slow down the fast path.

A panic:

1. flushes the standard output buffer (owned by the runtime, see below);
2. prints `panic: <message>` and `  at <file>:<line>:<col>` to standard error;
3. exits with status 101.

### Standard output buffer

The buffer for standard output lives **in the runtime**, not in `std.io`,
so that the runtime can flush it on panic and at exit without calling back
into Ofir code. It is line buffered when standard output is a terminal and
block buffered (64 KiB) otherwise. Writing to standard error first flushes
standard output, so messages keep their order.

A program with no tasks writes to the buffer without a lock. The first
`ofir_task_spawn` switches every later write to the threaded path: a lock
guards the buffer, and each thread gathers its writes into a line of its
own (1 KiB, thread-local) that reaches the buffer whole at a newline, when
it fills, at a flush, and when the task ends. So the pieces of one
`println` never interleave with another task's output.

### Tasks

`task.c` is linked only into programs that use `std.task`, because only
`std.task` references `ofir_task_spawn`. `std.task` hands the thread a job
on the heap, and the thread calls back into Ofir through
`ofir_task_entry`, which `std.task` exports. A task that panics ends the
whole process, as a panic on the main thread does, and a stack overflow in
a task is not yet reported as one: the guard handler of section 3 covers
the main thread only.

### Channels

`channel.c`, linked only into programs that use `std.sync`, keeps a queue
of pointers under a mutex, with a condition variable that `recv` waits on.
`std.sync` moves each value to memory of its own before `send` and back
out after `recv`, so the runtime never knows a value's type. The channel
counts its senders and knows whether the receiver is alive. The receiver
closes the channel first, so that later sends fail and drop their value,
then drops what it never took; the last of the senders and the receiver to
go frees the channel.

### Locks and atomics

`sync.c` wraps the compiler's `__atomic` builtins, all sequentially
consistent, on the `int` that `std.sync` keeps in an `UnsafeCell`. A mutex,
a read-write lock or a wait group (a count, a mutex and a condition
variable that `wait` waits on until the count is 0) lives on the heap, so
the Ofir value that owns it can move; its drop frees it.

## 3. Stack overflow

Deep recursion must not end in an anonymous segmentation fault. At startup
the runtime installs a `SIGSEGV` handler on an alternate signal stack
(`sigaltstack`). When a fault address lies in the guard page below the main
thread's stack, the handler prints

```
panic: stack overflow
```

and exits with status 101, like any other panic. Other faults are re-raised
unchanged.

Stage 0 (`runtime/src/stack.c`) takes the address of a local of `main` as
the top of the stack and the soft `RLIMIT_STACK` (8 MiB when unlimited)
as its size; a fault from there down to 1 MiB past the limit, which
covers the guard page and the frames that jump over it, is an overflow.
It handles `SIGBUS` too, which macOS raises for some overflows.

## 4. Rules

- The runtime has no global constructors and starts no thread of its own:
  threads exist only for the tasks a program starts.
- It never calls back into Ofir code, except `ofir_main` and
  `ofir_task_entry`.
- Every function is prefixed `ofir_` to avoid clashes with user code.
- It must compile without warnings with `-std=c11 -Wall -Wextra -Werror`.

## 5. Evolution

As the standard library grows, functions move from C to Ofir using
`extern "C"` declarations of libc functions. The long-term goal is a runtime
containing only the process entry point and the panic handler.
