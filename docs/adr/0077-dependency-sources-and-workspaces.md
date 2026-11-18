# 0077. Dependency scopes, overrides, vendoring and workspaces

- Status: Accepted
- Date: 2026-11-18

## Context

OFIR-65 asks for workspaces and dependency sources: path, git and
registry dependencies, dev, build and target dependencies, overrides,
vendoring and offline contracts. Path, git and registry sources exist
(ADR 0032, roadmap 6.5), with `--offline` and `--locked`. Missing:

- dependencies that only tests need, which a package's users should not
  fetch;
- dependencies for one operating system only;
- a way to replace a package everywhere in the graph, for a local fix or
  a fork, without editing the manifests of the packages that use it;
- a way to build with no network and no cache: the sources of every
  package kept in the repository;
- several packages developed together, with one lock.

Cargo has `dev-dependencies`, `[target.'cfg(..)'.dependencies]`,
`[patch]`, `cargo vendor` and workspaces; npm and pnpm have
`devDependencies`, `overrides` and workspaces; Go has `replace` and
`go mod vendor`. Ofir has no build scripts (OFIR-66), so it has no build
dependencies.

## Decision

**Dev dependencies.** The root's `devDependencies`, written like
`dependencies`, are in the graph only under `ofir test` (and `ofir
bench`). They are resolved and locked by every command, so the lock does
not depend on the command. The `devDependencies` of a dependency are
never read. A key in both `dependencies` and `devDependencies` is E0917.

**Target dependencies.** A dependency object may have `"os"`: `"linux"`,
`"macos"` or `"windows"`. It is in the graph only when the program is
built for that operating system (`--target`, ADR 0066). Like dev
dependencies, it is resolved and locked whatever the target, so one lock
serves every target.

**Overrides.** The root's `"overrides": {"key": {"path": "dir"}}`
replaces the source of every dependency whose key is `key`, in every
manifest of the graph, by the directory `dir`, relative to the root.
Only `path` overrides exist: a fork in git is checked out first. An
overridden package is not in the lock. The `overrides` of a dependency
are never read.

**Vendoring.** `ofir vendor [dir]` copies the source of each git and
registry package of the lock into `dir/<name>` (default `vendor`) and
prints the `overrides` that make the build use them. With those
overrides the build reads no cache and no network: `--offline` holds
with an empty `$OFIR_HOME`.

**Workspaces.** A manifest with `"workspace": {"members": [dirs]}` is a
workspace root. Its members are projects in those directories, relative
to it. One `ofir.lock`, next to the workspace root's manifest, holds the
packages of all members, and `ofir fetch` there resolves them together.
A build in a member finds its workspace root, the nearest directory above
it whose manifest lists it, and uses that lock and that root's
`overrides`. The `overrides` of a member are not read.

## Consequences

- A library's test dependencies cost its users nothing.
- A lock is the same on every OS and for every command; it may name
  packages that a given build does not use.
- Overrides by key are simple to state, but a package reached under
  another key is not overridden: override each key.
- A vendored repository builds on a machine with no network.
- Members of a workspace share one version of each package.
