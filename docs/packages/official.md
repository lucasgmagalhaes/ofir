# Official packages

An official package is a package of the default index
(`https://github.com/ofir-lang/index`) that the project maintains and
recommends (ADR 0097). This page lists the criteria a package meets to
become one and to stay one, and the packages that meet them.

## Criteria

A package is official while all of these hold for its latest version
that is not yanked:

1. **Metadata.** Its manifest has a `license` (an SPDX expression, ADR
   0081), a `description`, at least one keyword and a `repository`, so
   `ofir search` finds it and shows what it is.
2. **Toolchain.** Its `toolchain` (ADR 0082) includes the latest release
   of the compiler. A minor release that leaves it out makes the owner
   publish a version that includes it within 30 days.
3. **Documentation.** Every public item has a doc comment, and `ofir doc`
   builds its pages without a warning.
4. **Tests.** `ofir test` passes on Linux, macOS and Windows in its
   repository's CI, against the latest release.
5. **Examples.** Its repository has at least one example program, built
   by its CI, and its module doc comment shows a use of it.
6. **Owner.** The table below names an owner, a maintainer of Ofir
   ([maintaining.md](../maintaining.md#maintainers)) or a contributor
   who has agreed to the deadlines of this page.
7. **Advisories.** A vulnerability is handled as in
   [SECURITY.md](../../SECURITY.md): confirmed within 14 days, fixed in a
   new version with an advisory of the index within 90 days.
8. **Dependencies.** It depends only on official packages, so the
   criteria hold for everything it brings in.

A package that stops meeting a criterion for 90 days is removed from the
table, in a pull request that says which criterion; it stays in the
index, and its versions stay usable.

## Becoming one

The owner opens a pull request that adds a row to the table below, with
links to the CI run and the documentation that show each criterion. A
maintainer reviews it like any other pull request
([maintaining.md](../maintaining.md#review)).

## Packages

| Package | Description | Owner | Since |
|---------|-------------|-------|-------|

No package is official yet: the index has none. The first candidates
are the domain packages of OFIR-83 to OFIR-85 (roadmap 6.31).
