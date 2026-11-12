# Compatibility, deprecation, editions and upgrades

Policy version 1, for the 0.x releases (OFIR-04, ADR 0037). It says what a
release may change, how a change is announced, and how a program moves to
the next release. It is not a stability promise: before 1.0 there is
none.

## Versions

- The compiler's version is `VERSION` in `compiler/main.of`, printed by
  `ofir version`; `make seed` names the seed after it (docs/bootstrap.md).
- `std` and the runtime ship with the compiler and carry its version. A
  program uses the `std` of the compiler that builds it; there is no
  separate `std` version to require.
- Packages carry their own semver versions in `ofir.json` (ADR 0032).

## Before 1.0: no stability promise

- A **minor** release (0.5.x to 0.6.0) may break source, `std`, the
  manifest, the lock file and the command line. Each break is listed in
  `CHANGELOG.md` with its migration (below).
- A **patch** release (0.5.0 to 0.5.1) does not reject or change the
  meaning of a program the previous patch accepted, except to fix a
  soundness bug or to reject a program the specification already rejects.
- Only the latest release gets fixes.
- What 1.0 promises is **[open]**: a later ADR decides it, with editions.
  Nothing else in the repository (a `full` row of
  docs/design/conformance.md, a version number, a test) is a promise.

## What a break is

| Surface | Breaking | Not breaking |
|---------|----------|--------------|
| Source | a program that built is rejected, or runs differently | accepting a program that was an error; a better message |
| `std` | removing or renaming a `pub` item, changing its signature or documented behavior | a new item in a module; a faster implementation |
| Prelude | a new name in the prelude, or a name taken out (ADR 0036) | - |
| Diagnostics | reusing a code for another meaning (codes are never reused, docs/errors) | new codes; new warnings; changed text, notes or help |
| Manifest and lock | a key that changes meaning or is removed; a lock that no longer reads | a new optional key |
| Command line | a removed or renamed command or flag | a new command or flag |

Not covered at all: the AIR, the LLVM IR and the other `--emit` outputs,
the modules of `compiler/`, the runtime's C ABI, symbol names, layouts that
are not `@repr(C)` (spec 12.1), and performance. A new warning can fail a
build that passes `--deny-warnings`; that is not a break.

## Deprecation

A `pub` declaration of `std`, or of a package, is not removed in the
release that replaces it:

1. The release that adds the replacement marks the old declaration
   `@deprecated("use `new`")` (spec 12.1) and keeps it as a shim that
   forwards to the replacement. Each use outside a deprecated declaration
   is warning W0006, naming the replacement; `@allow(deprecated)` silences
   it while code migrates.
2. A later minor release may remove it, under "Removed" in the changelog.

Some changes cannot keep a shim: syntax, a keyword, a name moved out of
the prelude. Before 1.0 such a change may land in one release; the old
form must then be an error whose help names the new form, as `assert`
without its import does since ADR 0036 (E0200, help: the import).

Package authors follow semver (ADR 0032): `^0.3` accepts 0.3.x only, so a
0.x minor release of a package may break its users, and a 1.x minor may
not. A version with a fault is yanked, not deleted.

## Editions

The manifest has an `edition` key (docs/design/toolchain.md). The only
edition is `"2027"`. Stage 1 accepts the key and selects nothing by it: a
program with the key builds as without it, and an unknown value is not
rejected yet.

**[planned]** An edition will let a release change syntax or defaults for
the packages that name the new edition, while packages on an older edition
keep building unchanged in the same program, and an unknown edition will
be an error. Which changes need an edition, rather than a deprecation, is
decided with the 1.0 promise.

## Upgrades

- Each release has a `CHANGELOG.md` section. A break goes under "Changed"
  or "Removed" and says what to write instead.
- Each break has a migration fixture: a test that the old form still
  builds with W0006, or is rejected with a diagnostic that names the new
  form, and a test that the new form runs.
- The compiler builds itself through every release (`tools/bootstrap_chain.sh`,
  docs/bootstrap.md), so `compiler/` itself migrates with each release.

### Migration fixtures

| Change | Fixture |
|--------|---------|
| A renamed function kept as a deprecated shim | `tests/run/attributes/deprecated_shim.of`, `tests/compile_fail/attributes/deprecated.of` |
| `assert` moved from the prelude to `std.testing` (ADR 0036) | `tests/compile_fail/names/assert_import.of` |
| The `edition` key of the manifest | `tests/run/packages/edition/main.of` |
