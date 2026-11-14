# 13. Memory layout and ABI

This chapter defines how values are represented in memory. Unless a type is
marked `@repr(C)`, layouts may change between compiler versions; only the
properties stated as **guaranteed** can be relied on.

## 13.1 Primitive types

| Type | Size (bytes) | Alignment |
|------|-------------:|----------:|
| `bool`, `i8`, `u8` | 1 | 1 |
| `i16`, `u16` | 2 | 2 |
| `i32`, `u32`, `f32` | 4 | 4 |
| `i64`, `u64`, `f64`, `int`, `uint` | 8 | 8 |
| `()` | 0 | 1 |

`uint` and pointers have the target's pointer size; Ofir supports only
64-bit targets in version 1.

## 13.2 Structs and tuples

- The compiler may reorder fields to minimize padding (default layout).
  Version 1 orders them by descending alignment, keeping declaration
  order among fields of equal alignment; this is not guaranteed.
- `@repr(C)` structs use C layout: declaration order, C padding rules.
  **Guaranteed.**
- `@packed` structs have no padding: each field starts right after the
  previous one and the struct has alignment 1. **Guaranteed.** Their fields
  must be `bool`, numbers or `@packed` structs (E0811), and a field of a
  `@packed` struct cannot be borrowed, with `&`, `&mut` or as a `mut`
  argument (E0812): its address may not be aligned.
- `@align(N)` structs (`N` a power of two up to 4096, the page size) have
  an alignment of at least `N` bytes, and a size that is a multiple of it,
  wherever they are stored: locals, globals, fields, enum payloads and the
  heap. **Guaranteed.** A struct cannot be
  both `@packed` and `@align(N)` (E0808).
- Zero-sized structs occupy no space in their container.

## 13.3 Enums

- An enum without `@repr(C)` is a 32-bit tag; an enum with payloads adds a
  union of the payloads in 8-byte units after it, so `Option<u8>` and
  `Option<int>` are both 16 bytes. This layout is not guaranteed. A
  compact layout (the smallest tag that fits the variants, payloads
  aligned only as their fields need) is **[planned]**
  ([ADR 0051](../adr/0051-layout-and-abi-contracts.md)).
- `@repr(C)` applies only to enums without payloads (E0808): such an enum
  is a C `int`, its variants numbered from 0 in declaration order.
  **Guaranteed.**
- **Niche optimization:** an enum with exactly two variants, one without
  fields and the other with a single field that is never null (`&T`,
  `&mut T`, `Box<T>` or a `fn` value), keeps no tag: it has the size and
  alignment of that field, and the variant without fields is represented
  by a null pointer in the field's first word. **Guaranteed** for
  `Option<&T>`, `Option<&mut T>` and `Option<Box<T>>`, which have the size
  of a pointer, and for `Option<fn(...)>`, which has the size of a `fn`
  value with `None` a null code pointer. `&dyn I` is a niche field too
  (its data pointer); `&[T]` and `&str` are not, since an empty one may
  hold a null data pointer.

## 13.4 Built-in types

| Type | Representation |
|------|----------------|
| `&T`, `&mut T`, `*T`, `Box<T>` (sized `T`) | one pointer |
| `&[T]` | pointer + length (16 bytes) |
| `&str` | the representation of `str` (24 bytes) |
| `&dyn I`, `Box<dyn I>` | data pointer + vtable pointer (16 bytes) |
| `str` | pointer + length + capacity (24 bytes) |
| `List<T>` | pointer + length + capacity (24 bytes) |
| `Map<K, V>` | dense entry list in insertion order + open-addressing index table |
| `fn(A) R`, `move fn(A) R` | code pointer + environment pointer (16 bytes) |
| `extern "C" fn(A) R` **[planned]** | code pointer (8 bytes) |

A `str` built from a literal points to read-only static memory and has
capacity 0; it is copied to the heap on first modification.

## 13.5 Calling convention

- Ofir functions use an unspecified internal calling convention, chosen by
  the compiler (currently LLVM's `fastcc` for non-exported functions).
- `extern "C"` functions use the C ABI of the target. **Guaranteed.**
- Large values passed by value may be passed by hidden pointer; this is
  invisible to the program because moved-from values cannot be observed.
- **Parameter modes** (chapter 5.3) are lowered as follows:

  | Mode | Passed as |
  |------|-----------|
  | borrow, Copy type up to 16 bytes | by value, in registers |
  | borrow, type with interior mutability (chapter 5.8) | pointer, never `readonly` |
  | borrow, other types | pointer, `noalias readonly` |
  | borrow, `[T]` / `str` / `dyn I` | fat pointer: data pointer + length or vtable |
  | `mut` | pointer, `noalias` |
  | `move` | by value; large values by hidden pointer to a caller-owned copy |

- `&mut T` values are `noalias`; `&T` values to types without interior
  mutability are `noalias readonly`.

## 13.6 Program startup and shutdown

1. The runtime entry point initializes the allocator and command line
   arguments, then calls `main`.
2. When `main` returns, all its locals have already been dropped; standard
   output buffers are flushed and the process exits with status 0 (or 1 if
   `main` returned `Err`).
3. Global constants are computed at compile time; there are no global
   constructors.

## 13.7 Allocation

All heap allocation goes through the global allocator in `std.mem`, which
defaults to the C library's `malloc`/`free`. Allocation failure panics.
An allocation for a type whose alignment is above 16 (an `@align(N)`
struct, or a value containing one) is aligned to it: in `Box<T>`,
`List<T>` and closure environments alike.
Custom allocators passed explicitly to collections are rejected: arenas
in `std.arena` serve that need
([ADR 0049](../adr/0049-arenas-not-allocator-parameters.md)).

## 13.8 Targets

The *host* is the machine the compiler runs on; the *target* is the
machine the program runs on. The layout rules of this chapter, `@cfg`'s
`os` and `arch` (12.4), `os.name()` and `os.arch()` describe the target.
The target is the host unless `--target=TRIPLE` names one of the
triples below ([ADR 0065](../adr/0065-target-tiers.md),
[ADR 0066](../adr/0066-cross-compilation.md)).

Every supported target is 64-bit and little-endian. Tier 1 targets are
built and tested on every change; tier 2 targets have the code for them
but no CI:

| Tier | Target | C ABI | OS (oldest tested) | Toolchain |
|------|--------|-------|--------------------|-----------|
| 1 | `x86_64-linux-gnu` | SysV x86-64 | Ubuntu 24.04, glibc | clang 17 to 21 |
| 1 | `aarch64-apple-darwin` | AAPCS64 (Apple) | macOS 15 | clang 17 to 21 |
| 1 | `x86_64-w64-windows-gnu` | Microsoft x64 | Windows Server 2025 | MinGW-w64 clang |
| 2 | `aarch64-linux-gnu` | AAPCS64 | | clang 17 to 21 |
| 2 | `x86_64-apple-darwin` | SysV x86-64 | | clang 17 to 21 |

Other targets, among them every 32-bit and big-endian one, are
unsupported.
