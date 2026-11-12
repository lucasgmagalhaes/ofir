# 0037. No stability promise before 1.0; deprecate before removing

- Status: Accepted
- Date: 2026-11-12

## Context

Ofir is at 0.5.0. Its specification, `std` and package format still
change from release to release (ADR 0036 moved `assert` out of the
prelude), and the manifest already has an `edition` key that nothing
reads. Users and package authors need to know what a release may break,
how they hear of it, and how to move; the project must not promise more
than it keeps.

Considered:

- **Rust**: semver for the compiler, no breaking changes after 1.0 except
  through editions, which packages choose one by one and which mix in one
  build.
- **Go**: the Go 1 compatibility promise; `go.mod`'s `go` line selects
  language behavior per module.
- **Zig**: no promise before 1.0; each release may break code, and the
  release notes say how to upgrade.

## Decision

- Before 1.0 there is no stability promise. A minor release may break
  source, `std`, the manifest, the lock file and the command line; a
  patch release does not, except for soundness fixes and programs the
  specification already rejects.
- A `pub` declaration is deprecated with `@deprecated` (W0006), kept as a
  shim, for at least one minor release before it is removed. A change
  that cannot keep a shim makes the old form an error whose help names
  the new one.
- Each break is in `CHANGELOG.md` with its migration and has a migration
  fixture in `tests/`.
- Diagnostic codes are never reused; their text is not stable.
- The `edition` key stays reserved: `"2027"` is the only edition, and the
  key selects nothing yet. What 1.0 promises, and which changes need an
  edition, is a later ADR.

The policy itself is docs/design/compatibility.md, versioned so that a
change to it is visible.

## Consequences

- Breaking changes stay cheap while the language is young, and each one
  costs a changelog entry and a fixture.
- A program written for compiler 0.5 may need changes for 0.6. Between
  packages, a `^0.5` requirement (ADR 0032) never selects a 0.6 release.
- Editions, with the mixing of editions in one program, are designed only
  when a promise needs them.
