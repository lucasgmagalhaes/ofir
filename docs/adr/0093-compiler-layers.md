# 0093. Layers of the stage 1 compiler

- Status: Proposed
- Date: 2026-11-20

## Context

The stage 1 compiler (`compiler/`) grew one folder per phase, but the
folders did not form layers: almost every file imported `sema`, and some
imports ran upwards.

- `sema/type_trace.of` imported `air.air_print` for `formatG`, so the
  checker depended on the IR built from its output.
- `driver/` imported `lsp/` for its JSON writer, while `lsp/` imports
  `driver/` to load modules: a cycle between two tools.
- `base/` imported `diag/` for `jsonString`, so the bottom layer was not
  the bottom.
- `driver/sha256.of` was a pure algorithm kept next to the build code
  that used it.

Nothing checked the direction of an import, so each new cross-folder
call could add another cycle. Large folders (`sema/` has 50 files,
`driver/` 26) mix several concerns, and splitting them is easier once the
edges between folders are known and kept.

## Decision

1. **The folders are layers.** The passes import only downwards:

   | Folder | May import |
   |--------|------------|
   | `base` | — |
   | `diag` | `base` |
   | `syntax` | `base`, `diag` |
   | `sema` | the above |
   | `air` | the above and `sema` |
   | `codegen` | the above and `air` |
   | `driver` | `base`, `diag`, `syntax`, `sema` |
   | `doc`, `fmt` | `base`, `diag`, `syntax` |
   | `lsp` | `base`, `diag`, `syntax`, `sema`, `driver` |
   | `main.of` | any |

   Every folder may import itself and `std`. `driver` loads and checks
   modules and manages packages; it does not lower or emit code, which
   `main.of` drives.
2. **Shared code moves down.** `formatG` moved to `base/float_text.of`,
   the JSON writer and `jsonString` to `base/json.of`, and SHA-256 to
   `base/sha256.of`. `syntax/token_print.formatG` stays: it prints the
   shortest text that reads back as the same value, a different
   algorithm.
3. **A tool checks it.** `tools/check_layers.py` reads the `import`
   lines of every file under `compiler/` and fails on an import its
   folder may not make, or on a folder that has no row. It runs in
   `make test-shard-1`. A new folder states its place in the tool and in
   this table.
4. **Moves are their own commits.** Reorganizing a folder starts with a
   commit that only moves files and rewrites the imports they need, with
   no other change, and the output of every stage stays the same. Such a
   commit may touch many files, an exception to the rule of small
   commits: the files cannot be moved one at a time and still build.
5. **Large folders are split later, one pull request each.** `driver`
   into packages, build and tools; `sema` into resolution, types,
   checking, comptime and moves; `air` into the IR, lowering,
   optimization and the interpreter; `syntax` into the lexer, the parser
   and the AST; and the commands of `main.of` into one file each. Each
   split adds its subfolders to the tool's layers.

## Consequences

- An import that points up a layer fails `make test` with the file and
  line, before it reaches review.
- The layers are coarse: inside a folder any file may import any other,
  and `driver` still depends on `sema` as a whole.
- A move commit is large but mechanical; reviewers check that it has no
  other change and that `make compare-stages` and `make verify` pass.
