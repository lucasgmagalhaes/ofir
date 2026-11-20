# 0097. Package discovery and official packages

- Status: Proposed
- Date: 2026-11-20

## Context

OFIR-82 (roadmap 6.31) asks for "searchable docs/compatibility metadata,
examples and criteria for maintained official packages".

An index line (ADR 0079) had a package's version, dependencies,
checksum, license and whether it was yanked: nothing said what a package
does or which compilers it builds with, and the only way to find a
package was to know its name. Nothing said which packages the project
stands behind either, so a user could not tell a maintained package
from an abandoned one.

## Decision

1. **Discovery metadata in the manifest.** `description` (one line of at
   most 160 bytes), `keywords` (at most 5 distinct words of lowercase
   letters, digits and `-`, each at most 24 bytes) and `repository` (an
   `https://` URL). All are optional; an invalid one is E0910, when the
   project is built and when it is published.
2. **The index line records them.** `ofir publish` adds `description`,
   `keywords`, `repository` and the manifest's `toolchain` (ADR 0082) to
   the line of the version, so each version carries the compilers it
   builds with. An older line without them stays valid.
3. **`ofir search <words>`** reads the index (`--index URL|DIR`, default
   `$OFIR_REGISTRY` or the official one; `--offline` uses the cached
   clone) and lists the packages whose name, description or keywords
   hold every word, ignoring case. Each is shown at its highest version
   that is not yanked, with its description, and with the toolchain it
   needs when this compiler is not in it; a package whose versions are
   all yanked is left out. An exact name comes first, the others by name.
   `--message-format=json` prints a JSON line per package. Nothing found
   exits with 1. The index is read in one `git grep`, so a search costs
   one process, not one per package.
4. **Official packages** are listed in `docs/packages/official.md`, with
   the criteria they meet: metadata, a toolchain that includes the latest
   release, documentation, tests on the three platforms, examples, a
   named owner, the advisory deadlines of SECURITY.md and only official
   dependencies. A package that stops meeting one for 90 days leaves the
   list, not the index.
5. **Documentation search stays with `ofir doc`** (ADR 0075): a package's
   pages have their own search; the index records metadata, not docs.

## Consequences

- `tools/check_search.py`, in `make test-stage1`, publishes packages to a
  local index and checks the index lines, the search results, the JSON
  output and the E0910 of invalid metadata.
- A website that lists the index's packages can read the same lines
  without the compiler.
- The criteria are a promise of the project; no package meets them yet,
  and the domain packages of OFIR-83 to OFIR-85 are the first
  candidates.
- Ranking by downloads or by recency needs data the index does not have;
  it would be a later decision.
