# Releasing

The checklist for a release (ADR 0084). What a minor and a patch release
may change is in docs/design/compatibility.md.

## A minor release (0.5.x to 0.6.0)

1. On `main`, set `VERSION` in `compiler/driver/version.of` and rename
   the `[Unreleased]` section of `CHANGELOG.md` to `[0.6.0] - <date>`.
   Every entry under "Changed" that breaks a program says how to migrate.
2. Copy the compatibility corpus: `tests/compat/0.6/` gets examples, run
   tests, a project with a dependency and a lock, a program that fails
   with its error code, and a test module, each with what the new
   compiler prints (`.out`, `.err`, `.args`). Remove the corpora older
   than the previous minor (`tests/compat/0.4/` when 0.6 is made).
   `python3 tools/check_compat.py` passes.
3. Create the branch `release/0.6` from that commit and push it.
4. Run the Release workflow on `release/0.6` with `version` 0.6.0 and
   `candidate` 1. It tags `v0.6.0-rc.1` and publishes a pre-release with
   the seed, the archives and `SHA256SUMS`.
5. Wait a week. A fix goes to `main`, then to `release/0.6` (below), and
   makes `candidate` 2.
6. Run the Release workflow on `release/0.6` with `version` 0.6.0 and no
   `candidate`. It promotes the last candidate on that commit: the seed
   and every archive must hash as the candidate's did.
7. Record the seed checksum in `seed/SEEDS.md` (docs/bootstrap.md).
8. On `main`, start a new `[Unreleased]` section.

## A patch release (0.6.0 to 0.6.1)

1. Each fix is merged to `main` first. A pull request to `release/0.6`
   cherry-picks it with `git cherry-pick -x`. Only fixes: a soundness
   bug, a crash, a miscompile, a wrong result, a security fix, or a build
   that fails on a supported platform or LLVM.
2. On `release/0.6`, set `VERSION` to 0.6.1 and add a `[0.6.1]` section
   with a "Fixed" entry for each fix.
3. CI on the branch runs `tools/check_compat.py`; the Release workflow
   also runs it with `--release-branch`, so the 0.6 corpus must pass
   unchanged: `tests/compat/breaks.json` takes no `0.6/` entry there.
4. Publish `candidate` 1 and promote it, the same day if nothing fails.

Only the latest minor gets patches: `release/0.6` takes none after 0.7.0.

## A break on `main`

When a change makes a corpus program fail, add a `CHANGELOG.md` entry
that announces it, and a line to `tests/compat/breaks.json`:
`"0.5/name.of": "<text of that entry>"`. Do not edit the corpus.

## The LLVM range

`tools/llvm.json` holds the oldest and newest supported LLVM;
`tools/check_llvm_matrix.py` (in `make check-docs`) checks the workflows
and docs against it.

- Within a month of an LLVM release, raise `newest`, then the matrices of
  `.github/workflows/ci.yml` and `release.yml` and the docs it names.
- Raise `oldest` only in a minor release, with a "Changed" entry, when the
  CI's distributions ship a newer LLVM or the IR needs it.
