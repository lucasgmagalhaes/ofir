# Security policy

## Supported versions

Only the latest minor release gets fixes (docs/releasing.md).

| Version | Supported |
|---------|-----------|
| 0.5.x | yes |
| older | no |

## Reporting a vulnerability

Do not open a public issue. Report it privately on GitHub: the
repository's **Security** tab, **Report a vulnerability**. Include:

- the version (`ofir version`) and the platform;
- what is affected: the compiler, the runtime, `std`, the installer or
  the package tools (`ofir fetch`, `ofir publish`, `ofir audit`);
- the smallest program, manifest or archive that shows it, and what
  happens.

A miscompilation that breaks memory safety in a program without
`unsafe`, a way for a package or a manifest to run or write outside its
project, and a checksum or signature check that can be passed are all
vulnerabilities.

## What happens next

- The report is acknowledged within 3 days.
- A maintainer confirms it, or explains why it is not a vulnerability,
  within 14 days.
- A fix is released as a patch of the supported minor within 90 days,
  with a GitHub security advisory and a `CHANGELOG.md` entry under
  "Security". The reporter is credited unless they ask not to be.

A vulnerability in a published package is reported to the package's
owner; the package index records it as an advisory (ADR 0081), which
`ofir audit` reports.

The process is in [docs/maintaining.md](docs/maintaining.md#security).
