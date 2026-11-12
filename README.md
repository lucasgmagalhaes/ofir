# Ofir

Ofir is a compiled, statically typed systems programming language with a
lightweight, Go-like syntax and Rust-like memory safety — without a garbage
collector.

> Status: milestone M1 reached: the stage 0 compiler (C) builds and runs
> programs made of functions, integers, floats, booleans, strings,
> `if`, `for` and checked arithmetic. Next: the core language (roadmap
> phase 2).

```sh
make                                   # the stage 0 compiler and the runtime
build/stage0/ofir run examples/hello.of
```

Requirements: a C11 compiler, `make`, Python 3 and clang 17 or later. On Windows: Git Bash,
clang and a MinGW-w64 toolchain, with `mingw32-make` in place of `make`; see
[Windows](docs/design/toolchain.md#windows) for the installation steps.

## Editor support

The [VS Code extension](editors/vscode/README.md) recognizes `.of` files as
Ofir, highlights syntax, configures comments and brackets, and displays the
Ofir logo. Install it locally using the commands in its README.

## Documentation

| Document | Content |
|----------|---------|
| [Specification](docs/spec/README.md) | the language, chapter by chapter, with the full grammar |
| [Standard library](docs/std/README.md) | API reference of `std` |
| [Error codes](docs/errors/README.md) | registry of compiler diagnostics |
| [Style guide](docs/style-guide.md) | formatting and naming of Ofir code |
| [Compiler architecture](docs/design/compiler-architecture.md) | phases, AIR, code generation |
| [Project structure](docs/design/project-structure.md) | where every part of the code lives |
| [Toolchain](docs/design/toolchain.md) | the `ofir` command and build process |
| [Runtime](docs/design/runtime.md) | the small C runtime |
| [Testing](docs/design/testing.md) | test suites and formats |
| [Performance](docs/design/performance.md) | benchmarks, profiling and the regression gate |
| [Decisions](docs/adr/README.md) | architecture decision records |
| [Bootstrap](docs/bootstrap.md) | the path to a self-hosted compiler |
| [Roadmap](docs/roadmap.md) | ordered implementation tasks |
| [Language completeness](docs/design/language-completeness.md) | language-wide capabilities, evidence, priorities and release gates |
| [API ecosystem plan](docs/design/api-ecosystem.md) | service capabilities, gaps, dependencies and delivery gates |
| [Risks](docs/risks.md) | risk register and mitigations |

## Goals

- **Simple** — small grammar, no semicolons, type inference, few keywords.
- **Expressive** — enums with data, pattern matching, generics and
  implicit interfaces, as in Go but with zero-cost static dispatch.
- **Robust** — no null, exhaustive `match`, explicit error handling with
  `Result` and `?`, immutable bindings by default, checked arithmetic.
- **Lightweight** — no GC, no hidden runtime, tiny binaries.
- **Fast** — ownership-based memory management and an LLVM backend.
- **Self-hosted** — the Ofir compiler is written in Ofir.

## Taste

```ofir
struct Point {
    x: float
    y: float

    fn length(self) float {
        return (self.x * self.x + self.y * self.y).sqrt()
    }
}

fn main() {
    const p = Point{x: 3.0, y: 4.0}
    println("length = {p.length()}")
}
```

## License

Ofir is distributed under the [MIT License](LICENSE).
