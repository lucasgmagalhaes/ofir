# Architecture Decision Records

Each record documents one significant decision: its context, the decision,
and its consequences. Records are immutable once accepted; a decision is
changed by writing a new record that supersedes the old one.

A record is Proposed while its pull request is open, Accepted when it
merges, Rejected when the decision goes against it, and Superseded by
NNNN when a later record replaces it (ADR 0038). The status in this table
starts with the same word as the record's status line.

| # | Decision | Status |
|---|----------|--------|
| [0001](0001-textual-llvm-ir-backend.md) | Generate textual LLVM IR and compile it with clang | Accepted |
| [0002](0002-ownership-with-second-class-references.md) | Ownership with second-class references, no lifetime annotations | Accepted (refined by 0014) |
| [0003](0003-self-hosting-with-c-bootstrap.md) | Self-hosted compiler bootstrapped from a C compiler | Accepted |
| [0004](0004-implicit-interfaces.md) | Implicit (structural) interfaces with static and dynamic dispatch | Accepted |
| [0005](0005-checked-arithmetic.md) | Integer arithmetic is checked in every build profile | Accepted |
| [0006](0006-errors-as-values.md) | Errors as values (`Result`, `?`), panics abort | Accepted |
| [0007](0007-monomorphized-generics.md) | Generics implemented by monomorphization | Accepted |
| [0008](0008-go-like-syntax.md) | Go-like syntax without semicolons | Accepted (termination rule superseded by 0009) |
| [0009](0009-parser-driven-newlines.md) | Parser-driven statement termination | Accepted |
| [0010](0010-non-escaping-function-parameters.md) | Non-escaping function values by default | Accepted |
| [0011](0011-ordered-deterministic-map.md) | Insertion-ordered `Map` with deterministic hashing | Accepted |
| [0012](0012-int-for-lengths-and-indices.md) | `int` for lengths and indices | Accepted |
| [0013](0013-camel-case-naming.md) | camelCase naming convention | Accepted |
| [0014](0014-parameter-modes.md) | Parameters are borrowed by default (`mut`, `move` modes) | Accepted |
| [0015](0015-methods-inside-types.md) | Methods are declared inside their type | Accepted |
| [0016](0016-call-and-binding-ergonomics.md) | Defaults, named arguments, `let ... else` and `??`; no overloading | Accepted |
| [0017](0017-compile-time-reflection.md) | Compile-time reflection instead of macros | Accepted |
| [0018](0018-performance-driven-development.md) | Performance-driven development | Accepted |
| [0019](0019-standard-library-in-ofir.md) | The standard library is written in Ofir, from stage 0 on | Accepted |
| [0020](0020-documentation-comments.md) | Documentation comments in the style of JSDoc | Accepted |
| [0021](0021-type-operators.md) | Type operators: `keyof`, indexed access, mapped and conditional types | Accepted |
| [0022](0022-union-types.md) | Union types as anonymous tagged enums | Accepted |
| [0023](0023-arrow-closures.md) | Closures are written as arrow functions | Accepted |
| [0024](0024-any.md) | `Any`: values of any type through `dyn Any` | Accepted (spelling superseded by 0039) |
| [0025](0025-name-ofir.md) | The language is named Ofir | Accepted |
| [0026](0026-manifest-jsonc.md) | The manifest is `ofir.json`, JSON with comments | Accepted |
| [0027](0027-lightweight-tasks.md) | Concurrency through lightweight tasks, without `async`/`await` | Accepted |
| [0028](0028-qualified-variants.md) | Variants are always named through their enum | Accepted |
| [0029](0029-interface-parameters.md) | An interface as a parameter type is an anonymous type parameter | Accepted |
| [0030](0030-send-sync.md) | `Send` and `Sync` are derived, closures carry `+ Send` | Accepted |
| [0031](0031-let-const.md) | `let` is mutable, `const` is immutable; `var` is removed | Accepted |
| [0032](0032-packages.md) | Packages: dependencies in `ofir.json`, `ofir.lock`, one version each | Accepted |
| [0033](0033-codegen-units.md) | Incremental and parallel builds: codegen units and an object cache | Accepted |
| [0034](0034-seeded-map-hasher.md) | Seeded `Map` hashing: SipHash keys and a per-map seed | Accepted |
| [0035](0035-comptime.md) | Compile-time code checked once and expanded per instance | Accepted |
| [0036](0036-testing-module.md) | Asserts and `Bencher` in `std.testing`, not in the prelude | Accepted |
| [0037](0037-compatibility-policy.md) | No stability promise before 1.0; deprecate before removing | Accepted |
| [0038](0038-feature-states.md) | Feature states in one registry; no feature flags before 1.0 | Accepted |
| [0039](0039-any-type.md) | `any`: a builtin `dyn` type for values of any type | Accepted |
| [0040](0040-iteration-without-lazy-chains.md) | Iteration contracts, without lazy adapter chains | Accepted |
| [0041](0041-small-block-pool.md) | A small-block pool behind ofir_alloc | Accepted |
| [0042](0042-atomics-and-thread-locals.md) | Atomic intrinsics, memory orderings and thread-local globals | Accepted |
| [0043](0043-patterns-and-bindings.md) | Patterns, `name @ pattern` bindings and labeled control flow | Accepted |
| [0044](0044-accumulator-recursion.md) | Accumulator recursion with a deferred overflow check | Accepted |

## Template

```markdown
# NNNN. Title

- Status: Proposed | Accepted | Rejected | Superseded by NNNN
- Date: YYYY-MM-DD

## Context
## Decision
## Consequences
```
