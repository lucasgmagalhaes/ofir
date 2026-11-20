# compiler — stage 1 compiler

The self-hosted Ofir compiler, written in Ofir. Until milestone M4 it must
use only the bootstrap subset defined in [`docs/bootstrap.md`](../docs/bootstrap.md).

Architecture: [`docs/design/compiler-architecture.md`](../docs/design/compiler-architecture.md).

`ofir.json` makes this directory a project: imports resolve from here
(`import base.span`). Each port is checked against stage 0:

```
make stage1           # build/stage1/ofir, built by stage 0
make test-stage1      # every test, with stage 1 as the compiler
make compare-stages   # stage 1's output equals stage 0's on every .of file
```

Stage 1 is a complete compiler with stage 0's command line (`ofir
build|run|test <file.of>`, `--emit=tokens|ast|names|types|air|llvm`,
`-o`, `--release`, `--deny-warnings`, `--message-format=short`,
`--stop-after=lex|parse|check`). It finds std through `OFIR_STD` and the
runtime library through `OFIR_RUNTIME`, or next to its executable, as
stage 0.

The folders are layers (ADR 0093): each imports only the folders above it
in this list, and `make check-layers` enforces it.

| Folder | Contents | May import |
|--------|----------|------------|
| `base/` | spans, interning, index vectors, number text, JSON, SHA-256 | — |
| `diag/` | diagnostics | `base` |
| `syntax/` | the lexer, the parser and the AST | `base`, `diag` |
| `sema/` | the resolver, the type checker and the move analysis | the above |
| `air/` | the AIR and the lowering | the above and `sema` |
| `codegen/` | the LLVM codegen | the above and `air` |
| `driver/` | module loading, packages and the build | `base`, `diag`, `syntax`, `sema` |
| `driver/pkg/` | manifests, resolution, the lock, git and the registry | `driver/pkg` |
| `driver/build/` | module loading, the object cache and the build | `driver/pkg` |
| `doc/`, `fmt/` | `ofir doc` and `ofir fmt` | `base`, `diag`, `syntax` |
| `lsp/` | `ofir lsp` | `base`, `diag`, `syntax`, `sema`, `driver` |
| `main.of` | the command line | any |
