# 11. Unsafe code and FFI

Most Ofir programs never need this chapter. `unsafe` exists so that the
standard library (allocation, `List`, `Map`, system calls) can be written in
Ofir itself, and so programs can call C libraries.

## 11.1 `unsafe` blocks and functions

```ofir
unsafe {
    const p = alloc(sizeOf<int>() * 4) as *mut int
    *p = 42
}

unsafe fn copyBytes(dst: *mut u8, src: *u8, n: uint) { ... }
```

Inside an `unsafe` block or function, and only there, code may:

1. dereference raw pointers;
2. call `unsafe` functions, including all `extern` functions;
3. convert between pointer types and between pointers and `uint`;
4. read fields of `union` types **[planned]**;
5. access mutable global variables.

Everything else (types, ownership, borrowing) is still checked inside
`unsafe`. A safe function containing an `unsafe` block promises that it
cannot cause undefined behavior for any input; this is the author's
responsibility.

## 11.2 Raw pointers

| Operation | Example |
|-----------|---------|
| from reference (safe) | `const p = &x as *int` |
| null pointer (safe) | `const p: *u8 = 0 as *u8` |
| dereference (unsafe) | `*p` |
| offset (unsafe) | `p.add(3)` |
| compare (safe) | `p == q` |

Raw pointers are Copy, carry no ownership and are not tracked by the borrow
checker.

## 11.3 Calling C

```ofir
extern "C" {
    fn strlen(s: *u8) uint
    fn getenv(name: *u8) *u8
}

fn cLen(s: str) uint {
    const c = s.toCString()          // owned, null-terminated copy
    return unsafe { strlen(c.ptr()) }
}
```

- Only types with a defined C layout may cross the boundary by value:
  integers, floats, `bool` and raw pointers (E0804). A `@repr(C)` struct or
  enum crosses behind a raw pointer; its fields keep the C layout
  ([13.2](13-memory-layout.md)). Passing `@repr(C)` structs and enums by
  value is **[planned]** ([ADR 0051](../adr/0051-layout-and-abi-contracts.md)),
  and so is calling a variadic C function such as `printf`: a C wrapper
  with fixed parameters calls it instead. C unions have no Ofir type; a
  `@repr(C)` struct of bytes with the union's size and alignment stands in
  ([ADR 0063](../adr/0063-c-ffi-contract.md)).
- The program links against the C library by default. Other libraries are
  linked with `@link("name")` on the `extern` block: the linker gets
  `-lname`, once per library, in the order the blocks are declared.
  `name` is a library name: letters, digits, `_`, `.`, `+` and `-`, not
  starting with `-` (E0809).

```ofir
@link("m")
extern "C" {
    fn cbrt(x: f64) f64
}
```

### C function pointers

```ofir
extern "C" {
    fn qsort(base: *mut u8, count: uint, size: uint, cmp: extern "C" fn(*u8, *u8) i32)
}

extern "C" fn byValue(a: *u8, b: *u8) i32 {
    const x = unsafe { *(a as *i64) }
    const y = unsafe { *(b as *i64) }
    return if x < y { -1 } else if x > y { 1 } else { 0 }
}
```

- `extern "C" fn(A, B) R` is the type of a pointer to a C function: one
  code pointer, 8 bytes, called with the C calling convention. It crosses
  the boundary by value, its parameter and result types follow the rule
  above (E0804), and its parameters have no modes. It is Copy.
- The name of an `extern "C"` function, with a body (11.4)
  or declared in an `extern "C"` block, is a value of this type where one is
  expected and the signatures are equal. A closure or an Ofir function is
  not (E0823): C cannot pass it an environment.
- Calling a C function pointer requires `unsafe` (E0803).
- A function lives as long as the program, so C may keep a callback. A
  callback's state goes through the C library's user-data pointer, a
  `*mut u8` the callback casts back; the program keeps that state alive
  while C may use it.

### Ownership, errno and threads

- Each side frees what it allocated. Ofir values live in the Ofir
  allocator, which C must not `free`; memory a C library returns is held
  as a raw pointer and given back to that library's free function. A
  buffer C fills is a `List<T>` passed as its `asPtr()` cast to `*mut T`,
  with its length.
- `os.errno()` reads the calling thread's C `errno`, and `os.setErrno(n)`
  sets it. Allocation may change `errno`, so a program reads it right
  after the call that set it.
- C may call an exported function from a thread Ofir did not start; its
  thread-local state starts empty there. A panic ends the process and
  never unwinds into C.

## 11.4 Exporting functions to C

```ofir
@export("calc_add")
extern "C" fn add(a: i32, b: i32) i32 {
    return a + b
}
```

- An `extern "C" fn` with a body is a C symbol named after the function;
  `@export("name")` names the symbol `name` instead, a C identifier
  (E0809). Ofir code still calls the function by its own name.
- `@export` applies only to an `extern "C" fn` with a body (E0808).
- A program defines each C symbol once: two `extern "C"` functions with the
  same symbol are an error (E0813).
- `main` and the symbols that start with `ofir_` belong to the runtime and
  the generated code: the runtime's `main` calls the program's `main`, and
  its other functions are named `ofir_...`. Outside `std`, an `extern "C"`
  function with one of these symbols is an error (E0819). Other symbols are
  not checked: one that the C library or a linked library also defines is a
  link error, or replaces that definition, as in C.

## 11.5 Mutable globals

```ofir
let COUNTER: int = 0          // mutable global: access requires unsafe
const LIMIT: int = 10         // constants are always safe
```

- Reading, writing or borrowing a mutable global is an unsafe operation
  (E0803), in the module that declares it and through a path
  (`stats.TOTAL`).
- The initializer is a constant expression (chapter 3.2); a call or any
  other run-time value is E0801.
- Both stages implement globals of numeric, `bool` and raw pointer types;
  other types are E0901. `0 as *T` with the literal `0` is the null
  pointer, a safe cast, and the only constant pointer: any other cast of an
  integer to a pointer in a constant is E0801.
- `@threadLocal let NAME: T = constant` gives each thread its own copy,
  made from the initializer when the thread starts; access requires
  `unsafe` as above. Both stages implement it. `@threadLocal` on anything
  but a mutable global is E0808. Its type must be Copy, because a thread
  ends without dropping its globals (E0822).
- *Planned (ADR 0042):* globals of fixed arrays and `@repr(C)` structs of
  those types.

## 11.6 Undefined behavior

Safe Ofir code cannot cause undefined behavior. Unsafe code causes undefined
behavior if it:

- dereferences a null, dangling or misaligned pointer;
- creates two live `&mut` to the same value, or a `&mut` and a `&` to it;
- reads uninitialized memory;
- produces an invalid value (a `bool` other than 0 or 1, an invalid enum tag,
  a `str` with invalid UTF-8);
- causes a data race.

The rest of this section states what each rule asks of unsafe code. A safe
function that wraps unsafe code (`List`, `Box`, `str`, `std.sync`) must keep
these rules for every input; its tests run under ASan, UBSan and valgrind
(`make check-asan`, `make check-leaks`). Questions this section leaves
open have no guaranteed answer: unsafe code must not depend on any
answer to them.

### Validity

A value is valid when its bytes are a value of its type:

| Type | Valid bytes |
|------|-------------|
| integers, floats | any bytes (every NaN is a valid float) |
| `bool` | 0 or 1 |
| enums | a declared tag, and valid fields for that variant |
| `&T`, `&mut T`, `Box<T>` | non-null, aligned, pointing to a valid `T` |
| `str` | a pointer, length and capacity of valid UTF-8 bytes |
| raw pointers | any address, including null |
| structs, tuples, arrays | every field and element valid; padding is not read |

Producing an invalid value, even one that is never read, is undefined
behavior. Unsafe code must not rely on the bytes of padding.

### Initialization

Memory from `alloc` or `realloc` is uninitialized. Unsafe code must write a
valid value to a place before it reads the place, drops it or creates a
reference to it. A write through a raw pointer, `*p = v`, moves `v` into
the place without dropping what was there, so it initializes uninitialized
memory (as `List.push` does); to replace a valid value that needs a drop,
read the old value out first.

### Alignment

A dereference of `*T` must use an address that is a multiple of
`alignOf<T>()` (chapter 13). The C library's `malloc` aligns memory to 16
bytes on the supported targets; `Box`, `List` and closures allocate a type
with a larger alignment with `ofir_alloc_aligned` (chapter 13.7). A
`@packed` struct has alignment 1, and borrowing one of its fields is an
error (E0812), because the field may be misaligned.

### Aliasing

While a `&mut T` is live, the value it points to is reached only through
it: no other reference and no raw pointer may read or write that value.
While a `&T` is live, no one may write the value. Function parameters that
are references are emitted as `noalias` (chapter 13.5), so breaking this
rule lets the optimizer change what the program does. Raw pointers may
alias each other freely; a raw pointer made from a reference, `&x as *T`,
may be used only while the borrow of `x` would still be allowed.

### Provenance

A pointer is valid for the allocation it came from: the one `alloc`
returned, or the value its reference borrowed. `p.add(n)` must stay within
that allocation, or one element past its end, and only the addresses
within it may be dereferenced. Comparing pointers with `==` compares
addresses and is always safe. **[open]** whether a pointer made from a
`uint` (`n as *T`) may reach an allocation other than the one whose address
produced `n`; until this is decided, unsafe code makes pointers from
integers only for addresses that came from the same allocation.

### Data races

A data race is two accesses to the same location from different tasks, at
least one of them a write, with neither ordered before the other. A task
is ordered after the code that started it with `task.run`, and `join`
orders the task before the code after it. Sending on a channel orders the
send before the receive that gets the value; unlocking a `Mutex` orders
the unlock before the next lock. Every `AtomicInt` and `AtomicBool`
operation is sequentially consistent. Safe code cannot race, because
`Send` and `Sync` (chapter 14.3) keep shared values behind these types.
**[open]** weaker atomic orderings, and the result of a racy read in unsafe
code: unsafe code must not race.
