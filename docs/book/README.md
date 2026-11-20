# The Ofir Book

A tutorial for programmers who are new to Ofir. It installs a released
compiler, writes and runs small programs, and explains ownership and
error handling through the errors the compiler reports. The
[language specification](../spec/README.md) and the
[toolchain guide](../design/toolchain.md) remain the references.

The book follows the released compiler `0.5.0`. Every Ofir program in
it is compiled by `tools/check_book.py` (ADR 0095): a program marked
`run` must print the output shown after it, one marked `error` must
report its code, and one marked `test` must pass `ofir test`.

## Chapters

1. [Getting started](01-getting-started.md)
2. [Basics](02-basics.md)
3. [Ownership](03-ownership.md)
4. [Errors](04-errors.md)
5. [Projects and tests](05-projects-and-tests.md)
