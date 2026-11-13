# 0048. Allocation failure policy; fallible allocation deferred

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-23 asks for a consistent answer to allocation size and alignment
overflow, zero-sized values and running out of memory. Before this ADR:

- `List.reserve` computed `cap * sizeOf<T>()` with a checked multiply, so
  a huge capacity panicked with a generic `integer overflow` inside std.
- The runtime turned a negative size into a one-byte block, so a size
  computed wrongly by unchecked code produced a block too small for it.
- A failed `malloc` already panicked with `out of memory`.
- Zero-sized values (`List<()>`, `Box<Unit>`) worked and leaked nothing.

## Decision

- A size in bytes that does not fit in an `int`, or a negative one,
  panics with `capacity overflow` before anything is allocated:
  `List.reserve` checks its arithmetic, and `ofir_alloc`, `ofir_realloc`
  and their aligned forms reject a negative size. The runtime check sits
  past the pool's fast path, so it costs small allocations nothing.
- A failed allocation panics with `out of memory`, as before.
- Zero bytes is a valid request: it returns a block that is freed like
  any other. Zero-sized values need no special case in user code.
- Allocation stays infallible. A fallible API (`tryReserve`,
  `Box.tryNew`, an allocator returning `Result`) needs an explicit design
  covering every container and the error type, and is deferred.
- Alignment comes only from `alignOf<T>()`, a power of two the compiler
  computes, so the runtime does not validate it.

## Consequences

- Both failures have one message each and are covered by tests in
  `tests/panic` (`alloc_capacity_overflow`, `alloc_out_of_memory`).
- Under ASan the out-of-memory test needs `allocator_may_return_null=1`,
  so that the huge request returns null instead of aborting.
- Programs that must survive allocation failure cannot yet; that is the
  deferred design.
