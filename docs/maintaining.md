# Maintaining Ofir

How issues and pull requests are handled, who releases, how security
reports are handled, and what keeps the project going when a maintainer
leaves (ADR 0096). How to contribute is in
[CONTRIBUTING.md](../CONTRIBUTING.md).

## Maintainers

| GitHub | Role | Areas |
|--------|------|-------|
| @lucasgmagalhaes | lead, release manager | all |

A maintainer can merge pull requests and run the Release workflow. The
lead also holds the accounts of [Access](#access). `.github/CODEOWNERS`
names the same people, so GitHub asks them to review.

A contributor becomes a maintainer after several merged pull requests in
an area, when a maintainer proposes it in a pull request that adds them
to this table and the other maintainers approve it.

## Triage

Every new issue gets a `kind:` label and, once understood, a
`priority:` label and a state label, within 7 days.

| Label | Meaning |
|-------|---------|
| `kind: bug` | the compiler, runtime, `std` or tools do the wrong thing |
| `kind: feature` | a new capability; a language change starts in `docs/spec` |
| `kind: docs` | the documentation is wrong, missing or unclear |
| `kind: question` | a usage question; answered, then closed |
| `priority: p0` | a miscompilation, a crash on valid code or a broken release |
| `priority: p1` | a wrong diagnostic, a regression or a missing piece of a done roadmap step |
| `priority: p2` | everything else |
| `needs: repro` | waiting for a program that shows the problem |
| `accepted` | it will be done; a roadmap step names it when it is large |
| `wontfix` | closed with the reason (an ADR or the spec when there is one) |
| `good first issue` | small, with the files to change named in the issue |

- A `p0` bug is fixed before other work; when it is in a release, the
  fix is a patch release (docs/releasing.md).
- An issue with `needs: repro` and no answer for 30 days is closed; it
  can be reopened with the program.
- A feature request is answered with the roadmap step or ADR that covers
  it, or becomes one. A request that conflicts with an accepted ADR is
  `wontfix` unless it brings a new argument, which goes in a new ADR.

## Review

- Every change reaches `main` through a pull request, merged without
  squashing after CI is green ([CONTRIBUTING.md](../CONTRIBUTING.md)).
- A pull request is reviewed within 7 days, by a maintainer of its area
  (`.github/CODEOWNERS`) other than its author when there is one. While
  the project has one maintainer, the maintainer's own pull requests
  merge on green CI.
- Review checks that the change follows the rules of CONTRIBUTING.md:
  tests with each fix and feature, the spec before a language change,
  error codes in the registry, the benchmark budget.
- A pull request waiting for its author for 30 days is closed; it can be
  reopened.

## Releases

The release manager of [Maintainers](#maintainers) makes every release,
following [docs/releasing.md](releasing.md):

- a minor release about once a quarter, with a candidate a week before;
- a patch release for a `p0` bug or a security fix of the supported
  minor, as soon as the fix is merged.

When the release manager is unavailable, another maintainer releases,
following the same checklist.

## Security

Reports arrive privately, through GitHub's private vulnerability
reporting ([SECURITY.md](../SECURITY.md)). A maintainer:

1. acknowledges the report within 3 days;
2. confirms it within 14 days, in a private security advisory with the
   reporter, or explains why it is not a vulnerability;
3. fixes it on a private branch of the advisory, with a test, and
   releases the fix as a patch of the supported minor within 90 days;
4. publishes the advisory, with a `CHANGELOG.md` entry under "Security"
   and credit to the reporter.

A vulnerability in a package of the index becomes an advisory of the
index (ADR 0081), not of this repository.

## Access

What the project needs to keep running, and who holds it. No secret is
stored in the repository or its workflows: the Release workflow uses
the workflow's own `GITHUB_TOKEN`.

| Account | Used for | Holder |
|---------|----------|--------|
| GitHub repository admin | settings, branch protection, private vulnerability reports, the Release workflow | @lucasgmagalhaes |
| Vercel project | the website of `site/` | @lucasgmagalhaes |

## Continuity

- Everything needed to build, test and release is in the repository:
  the bootstrap from the C stage 0 (docs/bootstrap.md), the release
  checklist (docs/releasing.md) and the decisions (docs/adr).
- Each account of [Access](#access) has a second holder once there is a
  second maintainer.
- When no maintainer has answered issues or pull requests for 90 days,
  any contributor may open an issue asking for maintainership. If no
  maintainer answers it within 30 more days, the holder of the GitHub
  repository admin grants access to the contributor with the most
  merged pull requests who asks for it; if that is not possible, the
  project may be continued in a fork, under the license
  ([LICENSE](../LICENSE)).
