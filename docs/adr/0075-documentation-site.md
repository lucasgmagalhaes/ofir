# 0075. A static documentation site: search, links, visibility, versions

- Status: Accepted
- Date: 2026-11-18

## Context

OFIR-63 asks for documentation generation and doc tests with search,
cross-links, visibility, versioning, offline access and runnable
examples.

`ofir doc` (6.4b) writes one HTML page per module and an index, from
the documentation comments, with public declarations only. Doc tests
(6.4a, spec 12.5) already compile and run each ```` ```ofir ```` block of
a comment under `ofir test`. The pages have no search, a `` `code` ``
span names a declaration without linking to it, and nothing says which
version of a package a page documents.

## Decision

1. **Search.** `ofir doc` writes `search.js` next to the pages: one
   entry per documented declaration (its name, module, anchor and the
   first paragraph of its comment as text) and a small script. Each
   page has a search box that filters the entries by a case-insensitive
   substring of the name and links to the matches. The index is a
   script, not a JSON file read with `fetch`, so the search works on
   pages opened from the disk.
2. **Cross-links.** A `` `code` `` span in a comment whose text names a
   documented declaration links to it: `name` or `Type.member` of the
   same module, `module.name` of another module, or a name declared in
   exactly one other module. Code blocks and signatures are not linked.
3. **Visibility.** Public declarations only, by default. `--private`
   documents private declarations, fields and methods too, for the
   maintainers of a package; the signature, as written, shows whether
   one is `pub`.
4. **Versions.** The `name` and `version` of the `ofir.json` in the
   current directory head the index and each page. A version is
   published by writing it to its own directory (`ofir doc -o
   build/doc/1.2.0`); there is no version switcher.
5. **Offline.** The pages and `search.js` reference nothing outside the
   output directory: no fonts, styles or scripts from the network.
6. **Runnable examples.** The examples are the doc tests: `ofir test`
   compiles and runs them, so the examples on the pages are the ones
   that ran.
7. **Checked.** `tools/check_doc_site.py`, run by `make test-stage1`,
   documents a two-module package and checks the search index, the
   links, `--private`, the version, that no page refers to the network,
   and that the package's examples pass under `ofir test`.

## Consequences

- The search scans one array in the browser: fine for the thousands of
  declarations of a package and std, not for a registry of packages.
- A name declared in several other modules is not linked; writing
  `module.name` links it.
- Hosting several versions side by side is a matter of directories.
