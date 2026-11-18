# 0084. Release engineering

- Status: Proposed
- Date: 2026-11-18

## Context

OFIR-71 asks for release candidates, a compatibility corpus, an LLVM
matrix, a patch policy and tests of the installed artifacts.

What exists: the Release workflow builds the seed, checks it on every CI
platform and both ends of the LLVM range, builds each archive on its own
runner and installs it (`tools/check_install.py`, ADR 0082), then tags
`v<version>` and attaches the seed, the archives and `SHA256SUMS`. The
compatibility policy (docs/design/compatibility.md, ADR 0037) says what a
minor and a patch release may change. Builds are reproducible (ADR 0083).

What is missing:

- a release goes out on its first build: nothing is published for people
  to try first;
- nothing checks that a patch release keeps the programs of the release
  before it working, or that a minor release lists what it breaks: the
  tests of `tests/run` change with the language;
- the LLVM range (ADR 0001: 17 to the latest) is written in two
  workflows, and nothing says when it moves;
- there is no branch to make a patch release from: `main` has the next
  minor's changes;
- the installed compiler is only checked with one program.

Rust ships a beta for six weeks and backports fixes to it; Go publishes
`rc1`, `rc2` from a release branch and patches only the two latest
releases; Zig tags releases from `master` and has no patches. Each runs a
corpus of user code against candidates (crater, Go's module tests).

## Decision

### Candidates

1. **A release starts as a candidate.** The Release workflow takes a
   `candidate` number: with it, it tags `v<version>-rc.<n>` and publishes
   a pre-release with the same seed, archives and `SHA256SUMS` a release
   has. `VERSION` stays `<version>`: a candidate is the release's bytes,
   not a different version, so `toolchain` ranges (ADR 0082) and the
   bootstrap chain see the version it will be.
2. **A release promotes its last candidate.** Without `candidate`, the
   workflow requires a tag `v<version>-rc.<n>` on the same commit, builds
   again, and fails unless every archive and the seed hash the same as the
   candidate's (ADR 0083). The release then carries the bytes people
   tried. A fix after a candidate makes a new candidate.
3. A minor release has at least one candidate published for a week; a
   patch release may promote its candidate the same day.

### Branches and patches

4. **The first candidate of a minor release creates `release/<major>.<minor>`**
   from `main`. Candidates and patch releases of that minor are made from
   it; `main` goes on with the next minor. CI runs on pushes to
   `release/**` as on `main`.
5. **A fix lands on `main` first**, then a pull request to the release
   branch cherry-picks it (`git cherry-pick -x`, so the commit names its
   origin). Only fixes go to a release branch: a soundness bug, a crash, a
   miscompile, a wrong result, a security fix, or a build that fails on a
   supported platform or LLVM. Each gets a "Fixed" entry in the patch's
   `CHANGELOG.md` section.
6. Only the latest minor release gets patches (as docs/design/compatibility.md
   already says); its branch stops taking them when the next minor is
   released.

### Compatibility corpus

7. **`tests/compat/<major>.<minor>/`** holds programs that the release
   `<major>.<minor>.0` builds and runs, each with its expected output:
   single files, a project with a dependency and a lock, a program that
   fails with its error code, and a test module. It is copied from the
   examples and tests when a minor release is made, and not edited after.
8. **`tools/check_compat.py`**, run by `make test`, builds and runs every
   corpus with a given compiler and compares the output. On a release
   branch every corpus of the same minor must pass unchanged. On `main`,
   a corpus file that fails is a break: it is listed in
   `tests/compat/breaks.json` with the `CHANGELOG.md` entry that announces
   it, and the tool fails on a listed break whose entry is not in the
   changelog, or on an unlisted failure. A break listed there is checked
   to still fail, so the list stays true.
9. Corpora older than the previous minor release are removed: before 1.0
   only one minor of compatibility is checked.

### LLVM matrix

10. **The supported LLVM range is declared once**, in `tools/llvm.json`
    (`oldest`, `newest`). The CI and Release workflows test both ends on
    Linux and macOS; `tools/check_llvm_matrix.py`, run by `make check-docs`,
    fails when a workflow's matrix, the README, docs/design/toolchain.md
    or the target table of spec section 13 disagree with it.
11. **`newest` moves within a month of an LLVM release**, in any release.
    **`oldest` moves only in a minor release**, listed under "Changed" in
    the changelog, and only when the distributions the CI runs on ship a
    newer one or the IR needs it.

### Installed artifacts

12. **The installed compiler runs the compatibility corpus.** After
    `install.sh` installs the archive into an empty prefix on each
    release runner, `tools/check_compat.py --installed PREFIX` runs every
    corpus with `PREFIX/bin/ofir`, with `OFIR_STD`, `OFIR_RUNTIME` and
    `OFIR_CLANG` unset where the platform allows, and from a directory
    outside the repository. `make test` runs the same check on the archive
    that `make dist` builds.

### Docs

13. `docs/releasing.md` is the checklist: the candidate, the branch, the
    week, the promotion, a patch, the corpus copy and the LLVM bump.
    No new error code.

## Consequences

- A user can try a release before it is final, and the final release is
  bit for bit what they tried.
- A patch release cannot change what an older program prints without the
  corpus failing, and a minor release cannot break one silently.
- The corpus is a cost: each minor copies some programs, and a break
  needs a changelog entry and a line in `breaks.json`.
- Release branches mean cherry-picks, and two branches to keep green for
  the time a minor is current.
- Raising the oldest LLVM is a decision recorded in the changelog, not a
  side effect of a CI image change.
