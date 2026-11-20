# 0095. The Ofir Book

- Status: Proposed
- Date: 2026-11-20

## Context

OFIR-80 (roadmap 6.10, 6.31) asks for a book, tutorials, a website and
a playground, accepted when "versioned installation, ownership/error
explanations and examples pass on clean machines".

The repository had a specification, design notes, per-module std pages
and the error explanations of `docs/errors/`, but nothing a newcomer
reads in order. Its programs live in prose: nothing compiles the
snippets of a guide, so a guide drifts from the compiler without anyone
noticing. While drafting the book, five snippets that looked right did
not compile (a `*v` on an `int`, `text.parseInt()`, a `return` in a
`match` arm, `println` of a `float`, a borrow written `ref names[0]`).

Checking the book against the compiler also found commands that
[the toolchain guide](../design/toolchain.md) documents and the
compiler does not have: `ofir new`, `ofir build` and `ofir run` without
a file (project mode), and a name filter for `ofir test`.

## Decision

1. **The book is `docs/book/`**, five chapters in Markdown: getting
   started (installation of a release, hello world, `ofir explain`),
   basics, ownership, errors, projects and tests. `README.md` lists the
   chapters in order and names the compiler version the book follows.
   It uses only commands the compiler has.
2. **Every program of the book is checked.** An `ofir` block has a
   `<!-- check: KIND -->` line before it:
   - `run`: it runs and prints the `text` block that follows;
   - `error EXXXX`: the compiler reports that code, and prints the
     `text` block that follows, if any;
   - `test [file=NAME]`: its tests pass, and print the `text` block
     that follows, if any.
3. **The text is checked too.** Each `ofir X.Y.Z` or `ofir-X.Y.Z` is the
   version of `compiler/driver/version.of`, so the installation steps
   name the current release; every cited `EXXXX`/`WXXXX` has an
   explanation in `docs/errors/`; `README.md` lists exactly the
   chapter files.
4. **`tools/check_book.py` runs these checks**: the text in `make
   check-docs`, the programs with stage 1 in `make test-stage1`, and
   with the installed compiler of a release, without the repository's
   `std` or runtime, in `tools/check_install.py`. That last run is the
   "clean machine" of the acceptance.
5. **The website is the book as it is in the repository.** Publishing
   it on a site is a later step that needs no new content.
6. **The playground waits** for a way to run untrusted programs (a
   WebAssembly target or a sandboxed service); neither exists, and the
   roadmap keeps it as a follow-up.

## Consequences

- A change to the compiler that breaks an example of the book, or
  changes its output or diagnostic, fails CI and updates the book in the
  same pull request.
- A release bump changes the installation chapter, since its version is
  checked.
- The gaps between the toolchain guide and the compiler (`ofir new`,
  project mode, the test filter) are a follow-up step of the roadmap
  (6.31c); the book adds them once they exist.
- More tutorials are new chapters, under the same checks.
