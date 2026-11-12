# 0041. A small-block pool behind ofir_alloc

- Status: Accepted
- Date: 2026-11-12

## Context

Every `Box`, `List` buffer, `Map` table and heap string comes from
`ofir_alloc`, which called `malloc`. Programs that build many small
objects spend most of their time in the C allocator: in
`bench/programs/binary_trees.of`, 72% of the instructions were in glibc
`malloc` and `free`. glibc pays for generality there: a header per block,
bins for every size, checks against corruption and the bookkeeping of
`realloc` and `free` on any pointer.

Ofir knows more than `malloc` does. Every block goes back through
`ofir_free` or `ofir_realloc`, and no Ofir code reads a block header.

## Decision

- Blocks of 1 to 512 bytes come from a pool in the runtime
  (`runtime/src/pool.c`); larger blocks and blocks aligned above 16 still
  come from the C allocator.
- The pool reserves one range of address space (16 GiB, `PROT_NONE`) the
  first time it is used, and commits it 64 KiB at a time. Each chunk holds
  blocks of one size class: 16 to 128 bytes by 16, to 256 by 32, to 512
  by 64. A table indexed by chunk gives the class, so a block has no
  header and `ofir_free` knows a pool block by its address alone.
- Each thread keeps a free list and a part of a chunk per class, with no
  locks. A list that reaches two batches (32 KiB each) gives one batch to
  a global list per class, behind a spinlock, where any thread takes it.
  A task's thread gives all its blocks back when it ends. So a block freed
  on another thread than the one that made it is reused.
- `ofir_realloc` keeps a pool block in place when the new size is in its
  class, and moves it otherwise.
- The pool never returns memory to the system.
- The pool is off on Windows, under AddressSanitizer (which must see every
  block), and when the environment sets `OFIR_SYSTEM_ALLOC`;
  `tools/check_leaks.py` sets it, so that valgrind sees every block.

## Consequences

- `binary_trees` runs 13% faster and executes 42% fewer instructions.
- A program's peak of small blocks stays committed until it exits, and a
  thread that frees a lot holds up to two batches per class.
- Running out of the reserved range falls back to `malloc`.
- Pointers from C (`getline`, `realpath`) must not reach `ofir_free`,
  as before; a pool block must not reach `free`.
- Valgrind and leak checkers see the pool as a few large mappings unless
  `OFIR_SYSTEM_ALLOC` is set.
