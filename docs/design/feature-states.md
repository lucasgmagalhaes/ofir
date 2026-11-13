# Feature states

Every feature of Ofir that is not available yet, and every one Ofir will
not have (OFIR-07 of the [capability inventory](language-completeness.md),
roadmap 6.22g, [ADR 0038](../adr/0038-feature-states.md)).
`tools/check_features.py`, run by `make check-docs`, checks that each
**[planned]** and **[open]** marker of the [specification](../spec/README.md)
has a row here, that every link exists, and that the
[ADR index](../adr/README.md) matches the status line of each record.

## States

| State | Meaning | Where it is listed |
|-------|---------|--------------------|
| available | specified, implemented by stage 1, tested | [conformance matrix](conformance.md), `full` |
| partial | some rules, stages or targets missing | [conformance matrix](conformance.md), with classified gaps |
| planned | designed in the specification, not implemented | this page; **[planned]** in the specification |
| open | not decided; programs must not depend on an answer | this page; **[open]** in the specification or the roadmap |
| rejected | decided against, with the reason | this page; the deciding ADR |

There are no feature flags: before 1.0 every feature may change
([ADR 0037](../adr/0037-compatibility-policy.md)), and a feature that is
not implemented is rejected with E0901 or a dedicated diagnostic.

**Owner** is the component the work lands in; the project maintainer
accepts the specification change or ADR. **Gate** is what must exist
before the feature changes state:

- planned to available: the specification section, `tests/run` and
  `tests/compile_fail` cases, both stages where the bootstrap subset
  needs it, and a `full` row in the conformance matrix;
- open to planned or rejected: an accepted ADR, then the specification
  change that marks or removes it;
- rejected to anything else: a new ADR that supersedes the deciding one.

## Planned

| Feature | Owner | Gate | Source |
|---------|-------|------|--------|
| Arena allocators in `std` | std | available gate | [spec 5.9](../spec/05-ownership.md) |
| `std.math` constants and functions | std | available gate | [spec 7.6](../spec/07-modules.md) |
| The rest of the standard library beyond the bootstrap subset | std | available gate, per module ([API inventory](api-ecosystem.md)) | [spec 7.6](../spec/07-modules.md) |
| Converting a `dyn` value to an interface it embeds (`dyn Both` to `dyn Named`) | stage 1 checker, codegen | available gate; a vtable layout for embedded interfaces | [spec 9.7](../spec/09-interfaces.md) |
| `dyn` of generic interfaces (`dyn Iterator<int>`) | stage 1 checker, codegen | available gate | [spec 9.7](../spec/09-interfaces.md) |
| Reading `union` fields in `unsafe` code | stage 1 checker, codegen | available gate; `union` types first (roadmap open question 9) | [spec 11.1](../spec/11-unsafe-ffi.md) |
| Warning for an unused private function, type or constant | stage 1 checker | available gate, with a W code | [spec 12.6](../spec/12-attributes-testing.md) |
| Warning for an unused `mut` or `let` | stage 1 checker | available gate, with a W code | [spec 12.6](../spec/12-attributes-testing.md) |
| Naming convention warning (W0007) | stage 1 checker | available gate | [spec 12.6](../spec/12-attributes-testing.md) |
| Custom allocators passed to collections | std, runtime | available gate; roadmap open question 8 | [spec 13.7](../spec/13-memory-layout.md) |
| Editions | driver, specification | ADR on the 1.0 promise | [compatibility policy](compatibility.md) |

## Open

| Question | Owner | Gate | Source |
|----------|-------|------|--------|
| Pointer provenance of integers cast to pointers | specification, codegen | ADR | [spec 11.6](../spec/11-unsafe-ffi.md) |
| Weaker atomic orderings; racy reads in unsafe code | specification, runtime | ADR | [spec 11.6](../spec/11-unsafe-ffi.md) |
| Lazy iterator chains or eager `List.map`/`filter` only | std | ADR before task 5.8 is extended | [roadmap, open question 1](../roadmap.md) |
| Slice, string and `name @ pattern` patterns | stage 1 parser and checker | ADR | [roadmap, open question 2](../roadmap.md) |
| Error context and cause chains in `std` | std | ADR | [roadmap, open question 4](../roadmap.md) |
| 128-bit integers | specification, codegen | ADR | [roadmap, open question 5](../roadmap.md) |
| The M:N scheduler for lightweight tasks | runtime | ADR refining ADR 0027 | [roadmap, open question 6](../roadmap.md) |
| Registry hosting, versioning and `pub import` re-exports | package manager | ADR refining ADR 0032 | [roadmap, open question 7](../roadmap.md) |
| Callbacks from C, variadic C functions, `union` | stage 1, runtime | ADR | [roadmap, open question 9](../roadmap.md) |
| What 1.0 promises | specification | ADR superseding part of ADR 0037 | [compatibility policy](compatibility.md) |

## Rejected

| Feature | Reason | Source |
|---------|--------|--------|
| Exceptions | errors are values (`Result`, `?`); panics abort | [ADR 0006](../adr/0006-errors-as-values.md) |
| Lifetime annotations | references are second class | [ADR 0002](../adr/0002-ownership-with-second-class-references.md) |
| Explicit interface implementations | interfaces are structural | [ADR 0004](../adr/0004-implicit-interfaces.md) |
| Unchecked arithmetic by build profile | arithmetic is checked in every profile | [ADR 0005](../adr/0005-checked-arithmetic.md) |
| Semicolons | statements end at newlines | [ADR 0008](../adr/0008-go-like-syntax.md) |
| Function overloading | defaults and named arguments instead | [ADR 0016](../adr/0016-call-and-binding-ergonomics.md) |
| Macros | compile-time reflection instead | [ADR 0017](../adr/0017-compile-time-reflection.md) |
| `async`/`await` | lightweight tasks instead | [ADR 0027](../adr/0027-lightweight-tasks.md) |
| Shorthand `.V` and bare prelude variants | variants are named through their enum (E0326) | [ADR 0028](../adr/0028-qualified-variants.md) |
| `var` | `let` is mutable, `const` immutable | [ADR 0031](../adr/0031-let-const.md) |
| Unicode identifiers and confusable checks | tokens are ASCII; a change needs an ADR | [spec 1.1](../spec/01-lexical.md) |
| Feature flags before 1.0 | every feature is unstable before 1.0 | [ADR 0038](../adr/0038-feature-states.md) |
