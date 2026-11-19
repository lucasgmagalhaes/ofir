# 0088. References as arguments to borrowed parameters

- Status: Accepted
- Date: 2026-11-19

## Context

A parameter borrows its argument by default (spec 5.3), so `xs: List<T>`
already passes the list by address. A function that holds a `&List<T>`,
for example from a `ref struct` field or a `const r = &list`, could not
pass it to such a parameter without writing `*r`. To avoid the `*`, many
functions in `compiler/` and `std/` declared `xs: &List<T>` instead, and
their callers then had to write `&list`. Both spellings borrow, and the
mix was confusing: a reader could not tell which one a function wanted.

## Decision

1. A `&T` or `&mut T` argument is accepted for a parameter of type `T`:
   the compiler inserts the dereference. The mode rules then apply to
   the dereferenced place, so a `mut` parameter needs a `&mut T`
   (E0315) and a `move` parameter rejects any reference (E0602).
2. In a generic call the parameter's type may hold type parameters, so
   only its head is compared: the same kind of type, and for a struct
   or an enum the same declaration. `first(r)` with `r: &List<int>` and
   `fn first<T>(xs: List<T>)` infers `T = int`.
3. The opposite direction is not added: a parameter of type `&T` still
   needs `&x`. An explicit reference parameter stays rare, and the `&`
   at the call site shows that a reference is formed.
4. Parameters of type `&List<T>` in `compiler/` and `std/` become
   `List<T>`; `&T` stays for locals, `ref struct` fields and returned
   borrows such as `Option<&T>`.

## Consequences

- The generated code does not change: the argument was a pointer to the
  list either way.
- A call reads the same whether the caller owns the value or holds a
  reference to it.
- Both stages implement the coercion, so the compiler's own source may
  use it.
