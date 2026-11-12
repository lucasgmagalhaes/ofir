# Contributing to Ofir

## Workflow

1. Pick a task from the [roadmap](docs/roadmap.md) (or open an issue).
2. Create a branch from `main`: `<type>/<short-description>`, e.g.
   `feat/lexer-interpolation`, `fix/parser-recovery`, `docs/spec-closures`.
3. Make **small commits** following [Conventional Commits](#commit-messages).
4. Run `make test` locally; open a pull request when it passes.
5. CI must be green before merging. Pull requests are merged without
   squashing, so every commit must build and make sense on its own.

## Commit messages

Format: `<type>(<optional scope>): <description>`, in English, imperative
mood, lower case, no final period.

| Type | Use |
|------|-----|
| `feat` | a new language or compiler feature |
| `fix` | a bug fix |
| `docs` | documentation only (`docs/`, READMEs) |
| `test` | adding or changing tests only |
| `refactor` | code change that neither fixes a bug nor adds a feature |
| `perf` | performance improvement |
| `build` | Makefiles, toolchain detection |
| `ci` | CI configuration |
| `chore` | anything else (repository maintenance) |

Common scopes: `lexer`, `parser`, `sema`, `air`, `borrow`, `codegen`,
`runtime`, `std`, `driver`, `spec`, `adr`, `roadmap`.

Breaking changes to the language add `!` after the type and a
`BREAKING CHANGE:` footer:

```
feat(spec)!: write return types without arrow

BREAKING CHANGE: function signatures no longer use ->.
```

## Rules

- Every bug fix comes with a test that reproduces it.
- Every language feature comes with `tests/run` **and** `tests/compile_fail`
  cases.
- A change to the language starts with a change to [`docs/spec`](docs/spec);
  significant decisions get an [ADR](docs/adr/README.md).
- An ADR is Proposed in its pull request and Accepted when it merges
  (ADR 0038). A feature that changes state (planned, open, rejected) updates
  the [feature state registry](docs/design/feature-states.md) in the same
  pull request; there are no feature flags before 1.0.
- New diagnostics get a code in the [registry](docs/errors/README.md) in the
  same commit.
- Performance work follows [performance.md](docs/design/performance.md):
  measure first, one optimization per `perf` commit with its measured
  effect in the message, and an entry in `bench/HISTORY.md`. Pull requests
  must not increase benchmark instruction counts by more than 3% without
  a stated reason.
- Until milestone M4, code in `compiler/` uses only the
  [bootstrap subset](docs/bootstrap.md#bootstrap-subset).

## Code style

- C (`bootstrap/`, `runtime/`): C11, 4-space indentation, `snake_case`,
  must compile with `-Wall -Wextra -Werror`.
- Ofir: the [style guide](docs/style-guide.md).
- All languages: the size rules of the style guide (files, functions,
  nesting, parameters), checked by `tools/check_sizes.py`.
- Python (test runner and tools): standard library only, Python ≥ 3.9.
