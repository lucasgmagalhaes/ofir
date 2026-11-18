# 0076. Dependency graphs: no cycles, deterministic resolution

- Status: Accepted
- Date: 2026-11-18

## Context

OFIR-64 asks that dependency resolution and locks (ADR 0032) be tested
for version conflicts, cycles, checksums and determinism. Conflicts are
E0912 and checksums E0914, both with tests. Two contracts were left
open:

- A cycle of packages (`a` depends on `b`, which depends on `a`) was
  accepted: each package was read once, so the build worked, but the
  graph had no order to build or publish its packages in.
- Nothing stated that resolving the same graph twice gives the same
  `ofir.lock`, whatever the order of the manifests' keys or of the
  fetches.

Cargo and Go reject package cycles; npm allows them, and its cycles are a
known source of half-initialized modules.

## Decision

**Cycles.** The packages of a program form a directed acyclic graph. A
dependency that leads back to a package on the path from the root to it
is an error, E0916, reported at that dependency's entry in its manifest,
with a note naming the packages of the cycle in order. Each cycle is
reported once, found by a depth-first walk from the root that visits the
dependencies in the order of their manifest. Imports between the modules
of one package may still form cycles (spec 7).

**Determinism.** Resolution is a function of the manifests, the lock and
the fetched sources only:

- the version chosen for a package does not depend on the order of the
  keys in `dependencies`, nor on which requirement is read first;
- `ofir.lock` is sorted by name and written byte for byte the same for the
  same graph;
- running `ofir fetch` again without changes rewrites nothing;
- the checksum of a package does not depend on the file system's order
  of directory entries or on the files' timestamps.

`tools/check_resolution.py` checks these on git and path fixtures: it
fetches a graph, fetches it again, fetches it with every manifest's keys
reversed, and compares the locks; it also checks that a cycle is E0916.

## Consequences

- A package graph can always be built and published leaves first.
- Two packages that need each other must be merged, or their shared part
  moved to a third package.
- A lock can be compared across machines and runs: a difference means a
  change of the inputs.
