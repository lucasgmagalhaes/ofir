# 0081. Supply-chain integrity and provenance

- Status: Proposed
- Date: 2026-11-18

## Context

OFIR-68 (API-47) asks for checksums, trust roots, advisories, license
metadata and an SBOM. Part of this is in place:

- `ofir.lock` records a SHA-256 checksum of each git and registry
  package's files, and a checkout or archive that does not match is
  E0914 (6.5d, 6.5e);
- versions in the registry are immutable and can be yanked, and a mirror
  is accepted only when its archives match the index's checksums
  (ADR 0079);
- `--locked` and `--offline` builds change nothing and fetch nothing.

Four gaps remain. A manifest says nothing about its license, so a
program cannot tell what it ships under. Nothing lists a program's
packages in a format other tools read. Nothing tells a user that a
locked version has a known vulnerability. And any registry or git host
named by any manifest in the graph is fetched: an index whose history is
rewritten, removing or replacing versions, is not noticed.

Cargo has `license` (SPDX), `cargo audit` over the RustSec database and
`cargo vet`; Go has a checksum database and `govulncheck`; npm has
`npm audit` and `npm sbom`. CycloneDX and SPDX are the two SBOM formats
in common use.

## Decision

### Licenses

1. **`ofir.json` has a `license`**: an SPDX license expression
   (`"MIT"`, `"MIT OR Apache-2.0"`, `"GPL-2.0-only WITH
   Classpath-exception-2.0"`), or `"LicenseRef-<id>"` for a license
   outside the SPDX list. An expression that does not parse, or names an
   identifier that is not on the SPDX list (checked case-insensitively,
   written as the list writes it), is E0910. The field is optional for
   building; `ofir publish` refuses a package without one (E0919).
2. **The index records it.** A published version's line gains
   `"license"`, so a registry package's license is known without
   downloading it. Lines written before this field have none.

### SBOM

3. **`ofir sbom [--format=cyclonedx|spdx]`** prints a software bill of
   materials of the program on standard output: CycloneDX 1.5 JSON by
   default, SPDX 2.3 JSON with `--format=spdx`. It reads `ofir.lock` and
   fails as `--locked` does (E0913) when the lock is missing or out of
   date; it fetches nothing beyond what `--locked` would.
   - The root package is the subject; each locked package is a
     component with its name, version (a git package without a tag: its
     commit), license, source URL and a package URL (`pkg:ofir/<name>@
     <version>` for the registry, `pkg:generic/<name>@<commit>?vcs_url=`
     for git).
   - The lock's checksum is recorded as a property, `ofir:checksum`,
     not as a file hash: it is a hash of the package's tree (ADR 0079),
     not of an archive.
   - `path` packages and overrides are components with no version and
     no checksum, marked as local.
   - The dependency graph of the lock is included.
   - The output is the same for the same lock, byte for byte: no
     timestamp and no serial number unless `SOURCE_DATE_EPOCH` is set,
     which sets the timestamp.

### Advisories

4. **Advisories live in the index**, next to the packages, so the git
   host that publishes the registry publishes them too and a mirror
   carries them. The file `advisories/<package file>` (the package's
   path under `advisories/`) has a JSON line per advisory:

   ```json
   {"id": "OFIR-2026-0001", "versions": "<1.4.2", "severity": "high",
    "summary": "...", "url": "...", "aliases": ["CVE-2026-1234"],
    "withdrawn": false}
   ```

   `versions` is a list of requirements in the syntax of dependencies,
   separated by `,` and all of which must hold (`">=1.0.0, <1.4.2"`), or
   several such lists separated by `||`. `severity` is `low`,
   `medium`, `high` or `critical`. An advisory is never removed; a wrong
   one is withdrawn.
5. **`ofir audit`** checks each locked registry package against the
   advisories of its index, fetching the index unless `--offline`. It
   prints each match with the package, its version, the advisory and
   the lowest version that fixes it, if any, and each yanked version in
   the lock as a warning. The exit status is 0 when nothing matches and
   1 when an advisory matches (E0920), following ADR 0053;
   `--message-format=json` prints the matches as diagnostics.
6. **A known advisory can be accepted.** The root's
   `"audit": {"ignore": ["OFIR-2026-0001"]}` turns a match into a note.
   Git and path packages have no advisories and are listed as not
   covered. `build`, `fetch` and `update` do not check advisories: they
   run offline from the lock, and an advisory published later must not
   break a build that worked. CI runs `ofir audit`.

### Trust roots

7. **The root may list the sources it trusts.**

   ```jsonc
   "trust": {
       "registries": ["https://github.com/ofir-lang/index"],
       "git": ["https://github.com/acme/", "git@github.com:acme/"]
   }
   ```

   With `trust`, a registry dependency anywhere in the graph whose index
   is not in `registries`, or a git dependency whose URL does not start
   with an entry of `git`, is E0921, before anything is fetched. Path
   dependencies and overrides are trusted: they are on the user's disk.
   Without `trust` every source is accepted, as today. A dependency's
   own `trust` is not read; in a workspace, the root's applies.
8. **The lock pins each index's commit.** `ofir.lock` gains
   `"indexes": [{"url", "commit"}]`, the commit of each registry index
   the resolution read. When an index is read again (`fetch`, `update`,
   or a build that needs a version the lock lacks), its new head must
   descend from the locked commit: if it does not, the index's history
   was rewritten and resolution fails with E0922, naming both commits.
   `ofir update --index-reset` accepts the new history and records it.
   With `--locked` the locked commit is the one read, so a locked build
   sees the index as it was.
9. **Index commits are not signed, for now.** The git host
   authenticates who pushes (ADR 0079), the lock's checksums fix what
   each version contains, and rule 8 catches history being rewritten.
   Signing would protect against a compromised host, but needs a way to
   distribute and rotate the signers' keys; it is left until a public
   registry has more than one maintainer.

### Errors

10. New codes: E0920 (a locked package has an advisory), E0921 (a source
    is not trusted), E0922 (an index's history was rewritten). An
    invalid `license`, `audit` or `trust` is E0910; a package published
    without a license is E0919.

## Consequences

- Publishing needs a `license`; existing packages in the index keep
  lines without one, and `ofir sbom` reports them as having no declared
  license (`NOASSERTION`).
- The SPDX license list is embedded in the compiler as data and updated
  by hand; an identifier newer than the embedded list is E0910 until
  then, and `LicenseRef-` is the workaround.
- `ofir.lock` gains `indexes`, so every lock with a registry package
  changes once when it is next written. A lock without it is read as
  having no pinned commit: the next resolution records one.
- The ancestry check runs `git merge-base --is-ancestor` on the bare
  clone of the index, which is already in the cache; it costs one
  process per index read.
- Advisories are as good as the registry's maintainers make them. The
  format has `aliases` so CVE and GHSA identifiers can be matched by
  other tools.
- Auditing git dependencies, `cargo vet`-style reviews and signed
  indexes are left for later.
- The work lands in three parts: `license` and `ofir sbom`; advisories
  and `ofir audit`; trust roots and the pinned index commit. Each adds
  a `tools/check_*.py` test run by `make test`.
