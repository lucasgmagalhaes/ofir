# 0049. Typed index arenas in std; no allocator parameters

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-28 asks whether Ofir needs custom allocators, arenas and
allocation-aware collections. It needs a measured workload benefit and
clear contracts for ownership and fragmentation.

The allocation-heavy program in `bench/programs` is binary-trees. It
builds and frees millions of two-child nodes. Three versions were built
with `--release` on the same machine (best of 3 runs):

| Version                                     | Time   | Peak RSS |
|---------------------------------------------|--------|----------|
| `Box<Tree>` nodes (the benchmark)           | 1.76 s | 67 MB    |
| `std.arena`: `Arena<Node>`, `Id<Node>` links | 0.49 s | 43 MB    |
| A bare `List<Node>` with `int` links        | 0.33 s | 18 MB    |

The gain comes from the shape of the data, not from a faster allocator:

- nodes that die together are stored together;
- a whole tree is freed with one `clear`, which keeps the storage for the
  next tree instead of returning each node to the pool;
- links are indices, not pointers to separate blocks.

The `Arena` version is bigger than the bare list because each `Id` holds
an era next to its index (16 bytes, not 8), and the enum adds a tag.

A pluggable allocator would not get these gains. It would cost:

- an allocator parameter (or a hidden allocator field) on `List`, `Map`,
  `Box` and every type that holds them;
- every container's values would carry a lifetime tied to the
  allocator, which the borrow rules (ADR 0045) do not express;
- moving a value between allocators would need its own rules.

## Decision

- Add `std.arena`:
  - `Arena<T>` owns values of one type, each made by `alloc`;
  - `Id<T>` is a `copy` handle that `get` and `getMut` turn back into
    `&T` and `&mut T`;
  - `clear` drops every value and keeps the storage;
  - dropping the arena drops what it still holds.
- Ids are not references, so values can link to each other, cycles
  included, without borrowing the arena. Safe code cannot reach freed
  memory: `get` checks the id's index and its era. An id made before
  the last `clear` panics instead of reaching the value that took its
  place.
- An id from another arena of the same type is not detected unless its
  index is out of range. This mistake is a wrong value, not undefined
  behaviour.
- Fragmentation is bounded by design: there is no per-value free. An
  arena only grows until `clear`, so values with different lifetimes
  belong in different arenas.
- Rejected for now: allocator parameters on std collections, a global
  allocator hook and untyped bump arenas that hand out references. Any
  of them needs a workload that a typed arena or `List` cannot serve,
  and a lifetime design for references into the allocator.

## Consequences

- Programs with graph- or tree-shaped data that dies together get most
  of the measured gain from std alone, with no language change and no
  `unsafe`.
- The benchmark keeps its `Box` version, so it still measures the general
  allocator. An arena version would compare a different program.
- A future allocator design starts from this ADR's measurements and must
  beat `Arena` on a named workload.
