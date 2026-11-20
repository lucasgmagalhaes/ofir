# AGENTS.md

Context for Codex and other coding agents working in this repository. Keep it
short: it is read at the start of every session.

## Project

Ofir: a compiled language (`.of` files) emitted as LLVM IR text and built
with clang.

- `bootstrap/`: stage 0 compiler, in C.
- `compiler/`: stage 1, self-hosted, in Ofir (bootstrap subset,
  `docs/bootstrap.md`).
- `std/`, `runtime/`, `tests/`, `tools/`, `docs/` (spec, ADRs, roadmap).
- Manifest: `ofir.json` (JSONC, camelCase keys; ADR 0026).
- Work follows `docs/roadmap.md`; check it for the current phase and tasks.

## Working rules

- Talk to the user in Portuguese. Code, docs, commits and PRs in English.
- Commit as you go, in small commits that each build: Conventional
  Commits (`CONTRIBUTING.md`), messages in English.
- Git author and committer of every commit: Lucas Gomes
  <lucasgsm88@gmail.com> (`git -c user.name=... -c user.email=...` if the
  config differs).
- Every change reaches `main` through a pull request, merged after CI is
  green. Never push to `main` directly or merge locally.
- A change to stage 0 or std must not raise benchmark instructions by more
  than 3% (`make bench-compare`).
- A language change starts in `docs/spec`; big decisions get an ADR; new
  diagnostics get a code in `docs/errors`.
- When another agent uses the same checkout, work in an isolated worktree.

## Commands

Prefer `tools/quiet.sh <target>`: it writes the full output to
`build/logs/<target>.log` and prints only the summary and failures. Read
the relevant part of the log, not the whole file.

```
tools/quiet.sh test             # everything CI runs
tools/quiet.sh compare-stages   # stage 1 output == stage 0 output
tools/quiet.sh test-stage1      # all tests with stage 1 as the compiler
tools/quiet.sh verify           # stage 2 and stage 3 emit the same .ll
python3 tests/runner/run_tests.py <filter>   # a subset of the tests
build/stage0/ofir build|run|test <file.of> [--emit=tokens|ast|names|types|air|llvm]
```

## Stage 0 pitfalls (when writing Ofir in compiler/ or std/)

- A return type goes on the same line as the signature.
- No multi-line strings; no leading `+` continuation outside parentheses.
- `const X = ref.field else { ... }` is E0602: use `match`.
- `self` is a keyword outside methods: do not use it as a binding name.
- `u64 as i64` is range checked: use `.wrappingCast<i64>()`.
- `list[i] = f()` where `f` grows `list` writes into freed memory: compute
  `f()` into a local first.
- Lines are at most 100 columns; no single-line block bodies
  (`tools/check_format.py`).

## Stage 1 internals

- Stage 1 must match stage 0 output exactly (`tools/compare_stages.py`),
  including `test --emit=air|llvm`.
- In the AIR, AST local `i` is AIR local `i + 1`. `GLUE_DROP = 0`,
  `GLUE_CLONE = -2`, `VARIANT_STEP = 2147483648`; an enum tag is field 0
  (i32).
- Move results: `Checker.moveFlags` (MOVES=1, CONSUMED=2,
  COERCION_CONSUMED=4) and `Checker.fnMoves`.

## Token hygiene

- Do not read large files whole (`compiler/codegen/llvm.of`,
  `compiler/air/lower/lower.of`, `compiler/air/lower/lower_expr.of`): search first, then read
  the relevant range.
- Filter command output (`tools/quiet.sh`, bounded searches, log excerpts).
- Use `git diff --stat` before looking at a full diff.
- Wait for CI through PR activity events, not repeated polling.

## graphify

This project has a knowledge graph at `graphify-out/` with god nodes,
community structure, and cross-file relationships.

- When the user types `/graphify`, use the installed graphify skill or
  instructions before doing anything else.
- For codebase questions, first run `graphify query "<question>"` when
  `graphify-out/graph.json` exists. Use `graphify path "<A>" "<B>"` for
  relationships and `graphify explain "<concept>"` for focused concepts.
- Dirty `graphify-out/` files are expected after hooks or incremental updates;
  they are not a reason to skip graphify. Only skip it if the task is about
  stale or incorrect graph output, or the user explicitly says not to use it.
- If `graphify-out/wiki/index.md` exists, use it for broad navigation instead
  of raw source browsing.
- Read `graphify-out/GRAPH_REPORT.md` only for broad architecture review or
  when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current
  (AST-only, no API cost).
