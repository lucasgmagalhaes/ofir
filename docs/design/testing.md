# Testing strategy

Tests are the safety net that makes self-hosting possible: every stage of the
compiler must pass the same suites.

## 1. Test kinds

| Suite | Directory | What is checked |
|-------|-----------|-----------------|
| Unit (C) | `bootstrap/tests/` | data structures of stage 0 (arena, map, interning) |
| Lexer | `tests/lex/` | token stream for a source file (`--emit=tokens`) |
| Parser | `tests/parse/` | AST dump (`--emit=ast`) |
| Run | `tests/run/` | program compiles, runs, and prints the expected output |
| Compile fail | `tests/compile_fail/` | expected errors with code, line and message |
| Panic | `tests/panic/` | program panics with the expected message and exit 101 |
| Borrow | `tests/borrow/` | accepted and rejected ownership programs |
| Codegen | `tests/codegen/` | patterns in the generated LLVM IR |
| Format | `tests/fmt/` | `ofir fmt` output (`*.fmt`), which formats to itself (stage 1) |
| Documentation | `tests/doc/` | `ofir doc --stdout` pages (`*.html`) (stage 1) |
| Language server | `tests/lsp/` | `ofir lsp` messages (`*.out`) for the client's (`*.lsp`) (stage 1) |
| Unit (Ofir) | `@test` functions in `compiler/` and `std/` | internal functions |
| Self-host | `tests/selfhost/` | stage 2 output == stage 3 output |
| Benchmarks | `bench/` | performance against C, Rust and Go baselines |

## 2. File formats

### Run tests

```
tests/run/structs/methods.of
tests/run/structs/methods.out          expected stdout
```

A program made of several modules is a directory whose root module is
`main.of`; the other files are its modules, not separate tests:

```
tests/run/modules/app/main.of          the test (root module)
tests/run/modules/app/main.out
tests/run/modules/app/util.of          module `util`
tests/run/modules/app/geometry/shapes.of   module `geometry.shapes`
```

### Compile-fail tests

Expected diagnostics are annotations on the line where they are reported:

```ofir
fn main() {
    const x = 1
    x = 2   // error[E0304]: cannot assign twice to immutable variable `x`
}
```

The runner checks that exactly the annotated errors are produced, matching
the **code and line** strictly and the message as a substring.

A `// note: text` line of its own below an annotation expects a note of
that diagnostic containing `text`, where the test's path is shortened to
its file name (`trace.of:7:9`). The runner reads the notes from
`--message-format=json`, under stage 1 only.

In a project test, errors in its `ofir.json` (a dependency that cannot
be found, say) are annotated there, as JSONC comments on their line;
the runner matches them by file, line and code.

Git dependencies name repositories the runner builds before the tests:
each directory `tests/git/<repo>/<X.Y.Z>/` is a commit tagged `vX.Y.Z` of
`build/git-fixtures/<repo>`, made in version order with fixed authors and
dates, so its hashes are the same on every machine. A project reaches
one by a relative `git` path. Each test runs with its own `$OFIR_HOME`, a
temporary directory, so tests running at once do not share a cache; the
`ofir.lock` files they write hold absolute paths and are not committed.

Stage 0 reports only the first error per function and has terser messages.
When testing stage 0, the runner therefore checks only that the **first**
annotated error of each function is reported with the right code and line,
and ignores messages. Tests that need several errors in one function or
exact messages are tagged `// requires: stage1`.

Warnings are annotated the same way (`// warning[W0001]: ...`). A test
that annotates at least one warning must annotate all of them; a test of
errors ignores warnings, such as the unused variables it needs to trigger
its errors. `run` tests are compiled with `--deny-warnings`: programs that
run, examples included, are warning-free.

Error codes come from the registry in [`docs/errors`](../errors/README.md).

### Panic tests

```ofir
// panic: index out of bounds: the length is 3 but the index is 5
fn main() {
    const xs = [1, 2, 3]
    const i = 5
    println("{xs[i]}")
}
```

### Codegen tests

```ofir
// CHECK: define internal fastcc i64 @_A4test3add
// CHECK-NOT: call
fn add(a: int, b: int) int {
    return a + b
}
```

## 3. Compiler interface used by the runner

| Command | Used by |
|---------|---------|
| `ofir build FILE --emit=tokens` (tokens on stdout) | `lex` |
| `ofir build FILE --emit=ast` (AST on stdout) | `parse` |
| `ofir run FILE` | `run`, `panic` |
| `ofir build FILE --message-format=short -o OUT` | `compile_fail`, `borrow` |
| `ofir build FILE --message-format=json -o OUT` | `compile_fail` with `// note:` lines (stage 1) |
| `ofir build FILE --emit=llvm -o OUT` (writes `OUT.ll`) | `codegen` |
| `ofir fmt --stdout FILE` | `fmt` |
| `ofir doc --stdout FILE` | `doc` |

With `--message-format=short`, each diagnostic is one line:
`file:line:col: error[E0300]: message` (or `warning[W0001]`).

## 4. Runner

`tests/runner/run_tests.py` discovers test files, runs them in parallel and
prints a summary. It accepts:

- `--compiler=<path>` to choose the stage under test;
- a path or substring filter;
- `--bless` to rewrite expected `.tokens`, `.ast` and `.out` files after an
  intended change (the diff is then reviewed in the commit).

The runner itself is tested by `tests/runner/selftest/run_selftest.py`,
which drives it with a fake compiler (`make runner-selftest`).

### Memory and robustness checks

| Command | What it checks |
|---------|----------------|
| `make test` | the stage 0 compiler itself runs under ASan and UBSan (debug build) for every suite, and `make test-std` runs the `@test` functions of std |
| `make check-leaks` | every program of `tests/run` and `examples` under valgrind: no leak, no invalid access |
| `make check-asan` | the same programs and `tests/panic`, with the generated code (clang `-fsanitize=address`) and the runtime (ASan + UBSan) instrumented, linked by gcc; outputs must still match |
| `python3 tools/fuzz_frontend.py --count N` | mutated sources (cut, spliced, extra tokens) must get diagnostics, never a crash, hang or sanitizer report |

CI runs `check-leaks` and `check-asan` on Linux.

## 5. Rules

1. Every bug fix comes with a test reproducing the bug.
2. Every new language feature comes with run tests **and** compile-fail tests
   for its misuse.
3. Test files are small: one behavior per file.
4. CI runs all suites on Linux, macOS and Windows for every pull request.
5. Stage 0 and stage 1 run the same `tests/run`, `tests/panic` and
   `tests/compile_fail` suites (stage 0 may skip tests tagged
   `// requires: stage1` for features outside the bootstrap subset;
   `// requires: unix` skips a test on Windows, `// requires: windows`
   everywhere else). A
   `// flags: ...` line adds compiler flags to a run or codegen test, e.g.
   `// flags: --release`.
