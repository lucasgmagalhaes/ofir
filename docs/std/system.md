# `std.io`, `std.fs`, `std.os`, `std.time`, `std.task`, `std.sync`

## Errors (`std.io`)

```ofir
pub struct IoError {
    pub kind: IoErrorKind
    pub message: str
}

pub copy enum IoErrorKind {
    NotFound  PermissionDenied  AlreadyExists  InvalidInput
    UnexpectedEof  Interrupted  Other
}
```

`IoError` satisfies `Display` and `Debug` (once interfaces exist, roadmap
5.1). `IoError.fromErrno(code)` builds one from a C `errno` value and
`IoError.new(kind, message)` from a message of the caller's.

## `std.io`

| Item | Description | S0 |
|------|-------------|:--:|
| `fn stdout() Stdout`, `fn stderr() Stderr` | handles to the runtime-buffered streams | |
| `fn stdin() Stdin` | | ✓ |
| `fn readLine(mut self, buf: mut str) Result<int, IoError>` | returns bytes read, `0` at end of input | ✓ |
| `fn readToString(mut self) Result<str, IoError>` | | ✓ |
| `fn read(mut self, n: int) Result<str, IoError>` | up to `n` bytes, fewer only at the end of input | |
| `interface Reader`, `interface Writer` | spec 9.4 | |
| `struct BufWriter<W>` | buffered writer over any `Writer` | |
| `fn flush() Result<(), IoError>` | flush standard output | ✓ |

## `std.fs`

| Function | Description | S0 |
|----------|-------------|:--:|
| `fn readToString(path: str) Result<str, IoError>` | | ✓ |
| `fn read(path: str) Result<List<u8>, IoError>` | | ✓ |
| `fn write(path: str, data: [u8]) Result<(), IoError>` | creates or truncates | ✓ |
| `fn writeString(path: str, text: str) Result<(), IoError>` | the same, for text | ✓ |
| `fn exists(path: str) bool` | | ✓ |
| `fn canonicalize(path: str) Result<str, IoError>` | the absolute path, without `.`, `..` or symbolic links | ✓ |
| `fn createDirAll(path: str) Result<(), IoError>` | | ✓ |
| `fn removeFile(path: str) Result<(), IoError>` | | ✓ |
| `fn rename(from: str, to: str) Result<(), IoError>` | replaces `to` if it exists | ✓ |
| `fn metadata(path: str) Result<Metadata, IoError>` | `Metadata` has `size` (bytes) and `modified` (nanoseconds since 1970) | ✓ |
| `fn touch(path: str) Result<(), IoError>` | sets the modification time to now | ✓ |
| `fn readDir(path: str) Result<List<DirEntry>, IoError>` | sorted by name, for determinism; `DirEntry` has `name` and `path` | ✓ |
| `struct File` with `open`, `create`, `read`, `write`, closed on drop | | |

`std.fs.path`: `join(a: str, b: str) str`, `parent(p: str) Option<&str>`,
`fileName(p: str) Option<&str>`, `extension(p: str) Option<&str>` — planned.

## `std.os`

| Function | Description | S0 |
|----------|-------------|:--:|
| `fn args() List<str>` | `args()[0]` is the program path | ✓ |
| `fn env(name: str) Option<str>` | | ✓ |
| `fn exit(code: int) !` | flushes standard output, runs no destructors | ✓ |
| `fn run(program: str, args: [str]) Result<Output, IoError>` | runs a process and waits: `spawn`, then `wait` | ✓ |
| `fn spawn(program: str, args: [str]) Result<Child, IoError>` | starts a process without waiting; its output goes to temporary files | ✓ |
| `fn wait(move self) Result<Output, IoError>` (on `Child`) | waits for the process and returns what `run` returns | ✓ |
| `fn cpuCount() int` | the processors online, at least 1 | ✓ |
| `fn currentDir() Result<str, IoError>` | | ✓ |
| `fn executablePath() Result<str, IoError>` | locates `OFIR_HOME` | ✓ |
| `fn name() str` | the target operating system: `"linux"`, `"macos"` or `"windows"` | ✓ |
| `fn arch() str` | the target architecture: `"x86_64"` or `"aarch64"` | ✓ |

```ofir
pub struct Output {
    pub status: int        // exit status, or -1 if killed by a signal
    pub stdout: str
    pub stderr: str
}
```

## `std.time`

Both stages (spec 14.5). Times are `int` nanoseconds on a monotonic clock.

- `now() int`, `sleep(ns: int)`; the constants `MICROSECOND`,
  `MILLISECOND` and `SECOND`.
- `Deadline`: `after(ns)`, `never()`, `instant(self) int`,
  `expired(self) bool`, `remaining(self) int` (0 once passed).

## `std.task`

Stage 1 only (spec 14.2).

- `run<T: Send>(f: move fn() T + Send) Task<T>`: starts a task on an OS
  thread of its own.
- `Task<T>.join(move self) T`: waits for the task and returns its result.
  A task dropped without a join is detached: it runs on, and its result
  is dropped when it ends.

## `std.sync`

Stage 1 only (spec 14.4).

- `channel<T: Send>() (Sender<T>, Receiver<T>)`: an unbounded channel.
- `Sender<T>.send(self, value: move T)`: never waits; the value is dropped
  if the receiver is gone.
- `Sender<T>.clone(self) Sender<T>`: another sender of the same channel.
- `Receiver<T>.recv(self) Option<T>`: waits for the next value; `None` once
  the queue is empty and every sender is dropped.
- `Receiver<T>.tryRecv(self) Option<T>`: the next value if one is waiting.
- `Receiver<T>.recvUntil(self, deadline: Deadline) Recv<T>`: `Value(value)`,
  `TimedOut` or `Closed` (spec 14.5).
- `boundedChannel<T: Send>(capacity: int)`: a channel holding at most
  `capacity` values; `send` waits while it is full, and a capacity below 1
  panics.

Dropping the receiver drops the values it never took.

- `Arc<T: Send + Sync>`: `new(value: move T)`, `get(self) &T`,
  `clone(self) Arc<T>` (another owner), `owners(self) int`. The last owner
  dropped drops the value.
- `Mutex<T>`: `new(value: move T)`, `lock(self) MutexGuard<T>`; the guard's
  `get` and `getMut` reach the value, and dropping it unlocks.
- `RwLock<T>`: `new(value: move T)`, `read(self) ReadGuard<T>` (`get`),
  `write(self) WriteGuard<T>` (`get`, `getMut`).
- `AtomicInt`: `new`, `load`, `store`, `fetchAdd` and `swap` (both return
  the old value), `compareExchange(expected, desired) bool`.
- `AtomicBool`: `new`, `load`, `store`, `swap`.
- `WaitGroup`: `new`, `add(count)` (panics below 0), `done()`, `wait()`,
  `waitUntil(deadline) bool`.
- `CancelToken`: `new`, `clone` (the same token), `cancel`, `isCancelled`,
  `wait`, `waitUntil(deadline) bool`; cancellation is cooperative
  (spec 14.5).

Stage 0: `std.io`, `std.fs` and `std.os` are written in Ofir over the C
runtime's `os.c` and `fs.c` (docs/design/runtime.md); `std.io.Buffer` is
the runtime's result buffer and `io.check(code)` turns a returned `errno`
value into a `Result`. `eprint` and `eprintln` are prelude functions
(std/core/print.of) that flush standard output first.
