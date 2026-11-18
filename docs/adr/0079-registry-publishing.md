# 0079. Registry publishing

- Status: Accepted
- Date: 2026-11-18

## Context

OFIR-67 asks for namespaces, authentication, immutable versions, yanking,
mirrors and recovery in the registry. The client (6.5e) reads a git index
whose file for a package has one JSON line per version, and downloads
archives from the index's `download` URL; nothing writes the index.

crates.io has its own server, API tokens, owners per crate and
`cargo yank`; Go has no registry and a checksum database with a proxy;
npm has scopes (`@org/name`).

## Decision

**The index is the registry.** Publishing is a commit to the index's git
repository, so the git host does the authentication (SSH keys or tokens)
and keeps the history. Ofir has no server and no token of its own.

**`ofir package`** writes the package's archive to
`build/package/<name>-<version>.tar.gz`: its files without `.git` and
`build`, and prints the checksum (`driver.lock.checksum`) of those files.

**`ofir publish [--index DIR] [--dry-run]`** packages the project, then,
in `DIR` (a clone of the index with a work tree; default
`$OFIR_REGISTRY` when it is a local path):

- appends `{"version", "dependencies", "checksum", "yanked": false}` to
  the package's file and commits it;
- copies the archive where the index's `download` URL points, when it is
  a `file://` URL; otherwise the archive is uploaded by hand, or by the
  host's CI, to that URL.

The commit is pushed by the user, as any commit. `--dry-run` checks and
packages without writing to the index. A package cannot be published
(E0919) when its name is not lowercase letters, digits and `-` (starting
with a letter, at most 64 bytes), its `version` is not `X.Y.Z`, a
dependency is a `path` or `git` one, or the version is already in the
index.

**Immutable versions.** A version is never written twice: a published
version's line is only changed by yanking. A fix is a new version.

**`ofir yank <version> [--undo] [--index DIR]`** sets `yanked` in the
line of the project's package and version, and commits. A yanked version
is not chosen for a new lock but is kept where it is locked (6.5e). An
unknown version is E0919.

**Namespaces.** Names are flat. Who may publish a name is the index's
policy, enforced by its host (for example a `CODEOWNERS` entry per
package file, or review of each pull request to the index). A name is
never reused: deleting a package's file is not allowed by the official
index.

**Mirrors.** `$OFIR_REGISTRY` can name a mirror of the index, and the
mirror's `config.json` can name a mirror of the archives. A mirror needs
no trust: every archive must have the index's checksum, and a locked
package the lock's checksum (E0914).

**Recovery.** A lost or changed archive is caught by its checksum. A
project keeps building without the registry from the cache
(`--offline`) or from its `vendor` directory (6.27); an index can be
rebuilt from any clone, since it is a git repository.

## Consequences

- Publishing needs no new service: a git host is a registry, and a
  company can run its own index next to the official one.
- The index's history records who published what and when.
- Ownership rules live in the index's host, not in the tool: a stolen
  key is handled by the host.
- There are no scopes; a name collision is settled by the index's
  maintainers.
