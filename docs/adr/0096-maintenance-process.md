# 0096. Maintenance and contribution process

- Status: Proposed
- Date: 2026-11-20

## Context

OFIR-81 (roadmap 6.31) asks for issue and review triage, release
responsibility, security reporting and maintainer continuity.

CONTRIBUTING.md says how to make a change and docs/releasing.md how to
cut a release, but nothing said who releases, how an issue is labelled
or answered, how long a pull request may wait, where a vulnerability
goes, or what happens when the only maintainer stops. The risk register
rates a single maintainer (R8) high impact. The repository had no
security policy, no issue templates and no CODEOWNERS, so a
vulnerability would arrive in a public issue.

## Decision

1. **`docs/maintaining.md` is the process.** It has the maintainers
   table (with roles and areas), the triage labels and what each means,
   the review rules and deadlines, who makes releases and when, the
   handling of a security report, the accounts the project needs and
   their holders, and the continuity rules.
2. **Security reports are private.** `SECURITY.md` names the supported
   minor, asks for GitHub's private vulnerability reporting, says what is
   a vulnerability, and gives deadlines: acknowledged in 3 days,
   confirmed in 14, fixed in a patch release with an advisory in 90.
   The issue chooser links the private report and disables blank issues.
   A package's vulnerability is an advisory of the index (ADR 0081).
3. **Triage is by labels.** `kind:` on every issue (the issue templates
   add it), `priority:` p0 to p2 and a state (`needs: repro`,
   `accepted`, `wontfix`). A `p0` bug in a release makes a patch
   release.
4. **Review.** A pull request is reviewed within 7 days by a maintainer
   of its area (CODEOWNERS) other than its author when one exists; while
   there is one maintainer, their pull requests merge on green CI, as
   today.
5. **Continuity.** No secret lives in the repository or its workflows;
   the accounts that do exist (repository admin, the Vercel site) are
   listed with their holders. Every account gets a second holder with
   the second maintainer. After 90 days without a maintainer answering,
   a contributor may ask for maintainership; 30 more days without an
   answer let the repository admin hand it over, and the license allows
   a fork otherwise.
6. **`tools/check_maintenance.py`**, in `make check-docs`, keeps the
   files consistent: SECURITY.md supports the compiler's minor, its
   deadlines are those of docs/maintaining.md, every template label is in
   the triage table, CODEOWNERS names the maintainers, every account
   holder is a maintainer, and no workflow reads a secret other than
   `GITHUB_TOKEN`.

## Consequences

- A minor release updates SECURITY.md (docs/releasing.md step 1), or
  `make check-docs` fails.
- A workflow that needs a secret also adds it to `## Access`, so the
  accounts a successor needs stay listed.
- The labels must exist on GitHub; creating them is a one-time setting
  of the repository, not checked by CI.
- The deadlines are promises of a volunteer project; changing one is a
  change to both files, which the check keeps together.
