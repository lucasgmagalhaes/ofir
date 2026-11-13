# 0050. Tasks stay on OS threads; no async/await, no green tasks yet

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-29 asks whether Ofir needs async/await, green tasks, or a way to
run under another language's async runtime. The decision must rest on
measured workloads and say how cancellation and references held across
a suspension behave.

ADR 0027 already rejects `async`/`await` and plans a later M:N scheduler
behind `task.run`. This ADR checks that plan against numbers.

A task today is an OS thread (`task.run`, spec 14.2). Measured with
`--release` on a 4-CPU Linux machine:

| Workload                                   | Result                  |
|--------------------------------------------|-------------------------|
| `task.run` then `join`, one at a time      | 35.5 µs per task        |
| 1,000 tasks alive, all blocked on one gate | started in 19 ms, 11 MB |
| 10,000 tasks alive                         | started in 258 ms, 91 MB |
| 30,000 tasks alive                         | started in 666 ms, 270 MB |

A blocked task costs about 9 KB of resident memory: the stack is
reserved but only touched pages count. The default limits on that
machine allow about 64,000 threads per user.

The programs Ofir targets now are compilers, tools, batch jobs and
servers with up to a few thousand connections. All of them fit in these
numbers. There is no `std.net` yet, so no workload needs more.

## Decision

1. **No `async`/`await`.** ADR 0027 stands: functions have no color, and
   a blocking call is an ordinary call.
2. **Tasks stay one OS thread each.** No M:N scheduler and no green
   stacks are added now. They come back when a measured workload needs
   more live tasks than threads allow, or starts tasks faster than
   35 µs each matters. The first likely one is a `std.net` server with
   tens of thousands of idle connections. Such a scheduler goes behind
   the same `task.run` API, so programs do not change.
3. **Cancellation is cooperative** (spec 14.5). A task checks a
   `CancelToken` or waits with a `Deadline`; nothing stops a task from
   outside. A task that is never told to stop is detached when its
   handle is dropped, as now.
4. **No references live across a suspension.** `task.run` takes a
   `move fn() T + Send`, so a task owns everything it uses. A blocked
   task is a blocked thread with its own stack; no frame is ever saved
   on the heap while it borrows from another frame. This rules out the
   lifetime problems of suspended futures by construction.
5. **Interoperability: a foreign call holds its thread.** A C function
   that blocks blocks only the task that called it. A library that
   brings its own event loop runs it on a task of its own and talks to
   other tasks through channels. Ofir does not plug into another
   language's executor.
6. `tests/run/tasks/many_alive.of` keeps a thousand tasks alive at once,
   so the cost stays within reach of the numbers above.

## Consequences

- No new syntax, types or runtime: the language stays as it is.
- Blocking I/O is the model. A server with many idle connections pays a
  thread each until the M:N scheduler exists.
- If a green-task scheduler is added, a task that calls blocking C code
  must not stall other tasks; that design gets its own ADR.
- Code written today keeps working under such a scheduler, because it
  only sees `task.run`, `join`, channels and the `std.sync` primitives.
