# 0092. Build and hosted-tool security

- Status: Proposed
- Date: 2026-11-20

## Context

OFIR-78 asks for the security of the compiler, the build and the hosted
tools: budgets for malformed source, path and command handling, cache
integrity, secret handling and playground isolation.

Some of it was already in place:

- Syntactic depth is bounded (E0101, ADR 0052), as are generic
  instantiation, recursive types, cycles and comptime calls; the
  frontend fuzzer replays its seeds in `make test` (ADR 0086).
- Both stages start clang, the linker and git with an argument vector
  (`posix_spawnp`, `CreateProcess` with quoted arguments): no command
  goes through a shell.
- Cached objects are written to a temporary name and renamed into
  place, and one that does not start with an object file's magic number
  is deleted (ADR 0033).
- Packages fetched from git or a registry are pinned by commit and
  checksum (E0914), and `trust` limits their sources (E0921, ADR 0081).

Three gaps remained:

1. **A fetched package could reach outside its checkout.** A git or
   registry package is code from someone else. Its `path` dependencies,
   its `build.root` and its `build.native` files were resolved from its
   checkout without a bound, so `"path": "../../../../home/me/work"` or
   `"native": ["/home/me/.ssh/x.c"]` compiled local files the user never
   named into the build, with no lock entry or checksum.
2. **A credential in a URL was kept.** A git URL such as
   `https://user:token@host/repo` was written into `ofir.lock`, which is
   committed, and shown in diagnostics.
3. **A cached object was trusted by its first four bytes.** A truncated
   or corrupted object with a valid header was linked, and the program
   failed at link time or, worse, ran wrong code.

There is no playground: no hosted service compiles or runs code for
others.

## Decision

1. **A fetched package stays inside its checkout (E0924).** The
   directory of a git or registry pin bounds every package reached from
   it: a `path` dependency, `build.root` and each `build.native` file
   must resolve, after symbolic links, inside that checkout. A path
   dependency that stays inside is bounded by the same checkout. Local
   packages, workspace members and the root's `overrides` have no bound:
   their paths are the user's own. Git sources stay governed by `trust`.
2. **A URL must not carry a credential (E0925).** A `git` dependency
   whose URL has a password (`user:secret@`), or an `http` or `https`
   URL with any user part (a bare token), is rejected before anything is
   fetched or locked; so is such an `OFIR_REGISTRY`. Credentials belong to
   git (a credential helper, SSH keys), which the compiler calls without
   seeing them. `ssh://git@host/...` and `git@host:...` are accepted.
3. **A cached object has a checksum.** Next to each `<hash>.o` the cache
   keeps `<hash>.sum`, the SHA-256 of the object, written before the
   rename. An object is used only when its file hashes to its sum;
   otherwise both are deleted and the unit is compiled again. The
   directory of a cache entry is the key, so an object cannot be used for
   another unit.
4. **Malformed source has budgets, checked by a tool.**
   `tools/check_build_security.py` gives both stages adversarial files
   (random bytes, NUL bytes, invalid UTF-8, a 1 MiB line, deep nesting,
   long operator chains, a 10 000-digit literal, many syntax errors) and
   requires each to end with a diagnostic, not a signal or a panic,
   within 10 seconds and 1 GiB of peak memory, with at most one error per
   byte of source. It also builds a program whose path holds spaces and shell
   metacharacters, and checks that no compiler or runtime source calls
   `system` or `popen` and no tool script uses `shell=True`. It runs in
   `make test-shard-1`.
5. **A playground waits, with its requirements written down.** A hosted
   playground must compile and run each request in a fresh sandbox with
   no network, a read-only toolchain, a scratch directory as the only
   writable path, limits on CPU time, wall time, memory, processes and
   output, and no secrets in its environment; the compiler's budgets
   above are a floor, not the isolation. It is built when there is a
   service to host it (OFIR-79 and later).

## Consequences

- A fetched package that relied on files outside its checkout no longer
  builds; it must vendor them or depend on a published package.
- A lock or a diagnostic never shows a credential that came from a URL.
- A cache hit hashes the object it uses: a warm self-compile reads and
  hashes its objects once more, a small cost against the clang runs it
  saves.
- The budgets bound the compiler on hostile input; they do not make it a
  sandbox. Running untrusted programs still needs an isolated machine.
