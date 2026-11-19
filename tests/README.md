# tests — test suites

| Suite | Content |
|-------|---------|
| `lex/` | `*.of` + `*.tokens`: expected token streams |
| `parse/` | `*.of` + `*.ast`: expected AST dumps |
| `run/` | `*.of` + `*.out`: programs and their expected standard output |
| `compile_fail/` | `*.of` with `// error[EXXXX]: message` annotations |
| `panic/` | `*.of` with a `// panic: message` header |
| `borrow/` | ownership and borrow checker cases |
| `codegen/` | `*.of` with `// CHECK:` lines matched against the LLVM IR |
| `fmt/` | `*.of` + `*.fmt`: `ofir fmt --stdout` output, which must format to itself (stage 1) |
| `doc/` | `*.of` + `*.html`: `ofir doc --stdout` output (stage 1) |
| `lsp/` | `*.of` + optional `*.lsp` (client messages) + `*.out`: `ofir lsp` messages (stage 1) |
| `selfhost/` | stage 2 == stage 3 verification |

Run everything with `make test`, or a subset with
`python3 tests/runner/run_tests.py --compiler build/stage0/ofir run/structs`.
Formats and rules: [`docs/design/testing.md`](../docs/design/testing.md).

Cases that use several features together live in `run/interactions/` and
`compile_fail/interactions/`. Each starts with a `// sections: N.M N.M ...`
line naming the specification sections it combines (after any
`// requires:` line), and has a row in the Interactions table of
[`docs/design/conformance.md`](../docs/design/conformance.md), which
`tools/check_conformance.py` checks (ADR 0085).
