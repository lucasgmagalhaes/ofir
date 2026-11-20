# Language completeness and ecosystem plan

Status: **proposed inventory**, initially surveyed at `58c340e` on
2026-11-04; API references reconciled on 2026-11-19.
This covers a general-purpose language, including its compiler, runtime,
libraries, tools, distribution and maintenance. It is not a specification or
a commitment to ship every capability in version 1.0.

The 86 OFIR records below are capability and acceptance records, not 86 missing
implementations. Many extend or audit existing roadmap tasks. The 54 records in
the [API ecosystem plan](api-ecosystem.md) form a subordinate service plan;
overlapping capabilities must share implementation tasks and must not be counted
twice. Existing roadmap completion markers remain authoritative.

## Evidence and coverage

This is a targeted repository survey, not a new conformance or correctness run.
The first deliverable is a maintained implementation matrix: every normative
feature needs its spec/ADR, compiler stage, source, positive/negative fixtures,
target coverage, diagnostics and latest verification evidence. Use these states:
**implemented and verified**, **partial**, **designed**, **proposed**, or
**deferred/rejected**. A chapter, syntax example or parser node alone does not
prove implementation. Stage 0 remains the bootstrap subset.

| Starting point | Evidence | Consequence |
|---|---|---|
| Bootstrap and test infrastructure exist | [Bootstrap](../bootstrap.md), [testing](testing.md), [stage comparison](../../tools/compare_stages.py) | Extend these gates; do not plan their creation again |
| Accepted design exceeds implemented language | [Specification](../spec/README.md), [roadmap](../roadmap.md) | Track per-feature conformance and explicit unsupported cases |
| Checker still has unsupported paths | [Expression checker](../../compiler/sema/check/expr_check.of), [type checker](../../compiler/sema/check/check_types.of) | Audit function values, named arguments, ranges outside loops, mutable slices and array lengths individually |
| Language support in VS Code is a starting point | [Extension manifest](../../editors/vscode/package.json) | Grammar and icons do not supply LSP or debugger integration |
| Standard-library reference contains planned APIs | [Utilities](../std/utilities.md), [collections](../std/collections.md), [API inventory](api-ecosystem.md) | Verify exported code and tests before labeling an API available |
| Several foundational choices are already accepted | [ADRs](../adr/README.md) | Respect ownership, errors as values and bootstrap constraints |

## Priorities and interpretation

- **G0:** establish implementation truth, safety and trustworthy builds.
- **G1:** proposed general-purpose baseline; select release scope explicitly.
- **G2:** optional specialization, justified by a concrete workload and owner.
- **A:** audit or extend existing work; **D:** already designed/planned;
  **N:** newly explicit backlog coverage; **X:** decision required, possibly reject.

Dependencies name existing roadmap items or related capability records. Each row
can require several small PRs. Completion means its evidence is recorded, not
that every possible extension has been implemented.

## 6.22 — Specification and compatibility

| ID | Priority/type | Capability and dependencies | Completion evidence |
|---|---|---|---|
| OFIR-01 | G0/N | Spec-to-implementation matrix, including std and stage/target differences | Source and fixture links for every guarantee; omissions explicitly classified |
| OFIR-02 | G0/A | Grammar, newline rules, evaluation order, literals and conversions | Positive and negative boundary cases; documented order of side effects |
| OFIR-03 | G0/A | Memory validity, initialization, alignment, aliasing, pointer provenance and data races | Unsafe obligations and unresolved semantics explicit; safe wrappers tested against them |
| OFIR-04 | G1/N | Source/std/package compatibility, deprecation, editions and upgrade policy | Versioned policy and migration fixtures; no accidental stability promise |
| OFIR-05 | G1/A | Equality, hashing, ordering, numeric casts, NaN/signed zero and seeded maps (6.7) | Consistent contracts and edge-case tests, including custom keys |
| OFIR-06 | G1/X | Source encoding, identifier normalization, confusables and diagnostic columns | Explicit ASCII/Unicode policy and encoding fixtures; Unicode expansion is optional |
| OFIR-07 | G0/N | Experimental/stable feature states and ADR acceptance | Features have owners, gates and documented flags or rejection decisions |

## 6.23 — Language completion

| ID | Priority/type | Capability and dependencies | Completion evidence |
|---|---|---|---|
| OFIR-08 | G1/D | Structural interfaces, defaults, embedding and derived behavior (5.3, 5.5) | Ambiguity, recursion and user-defined type cases pass |
| OFIR-09 | G1/D | Dynamic dispatch, unsized values and Any (5.4) | Object safety, downcasts, ownership and vtable layout verified |
| OFIR-10 | G1/D | Closures/function values (5.7) | Capture, escape, borrow, move, drop and thread-transfer rules tested |
| OFIR-11 | G1/D | Generic std and iteration (5.6, 5.8) | Borrowed/mutable/consuming iteration and invalidation contracts; decide lazy chains |
| OFIR-12 | G1/D | Named/default arguments and string forms/formatting (5.10, 5.12a) | Evaluation order, diagnostics, raw/multiline strings and UTF-8 boundaries tested |
| OFIR-13 | G1/D | Attributes and conditional compilation (5.11) | Unknown attributes, target conditions, exports and deprecation behavior documented |
| OFIR-14 | G1/D | Compile-time execution and reflection (6.7a) | Deterministic evaluation, resource limits, diagnostics and source locations |
| OFIR-15 | G1/D | Key/indexed, mapped and conditional types (5.17–5.19) | Visibility, inference, ownership and recursion limits; retain completed alias support |
| OFIR-16 | G1/D | Union types and narrowing (5.20–5.21) | Normalization, exhaustiveness, layout and invalid narrowing tested |
| OFIR-17 | G1/D | Const generics and array-length expressions (6.7) | Inference, specialization and evaluation bounds specified and tested |
| OFIR-18 | G1/X | Additional patterns, guards, bindings and labeled control flow | Resolve roadmap questions with examples and diagnostics before extending syntax |
| OFIR-19 | G1/A | Borrow usability: mutable slices, reborrows, disjoint fields and reference returns | Accepted safe cases and rejected lifetime/alias violations paired in fixtures |
| OFIR-20 | G1/A | Modules, nested paths, visibility, re-exports and dependency cycles | Cross-module resolution and initialization policy tested |
| OFIR-21 | G1/X | Remaining numeric/type gaps: range values, 128-bit integers and array operations | Decide each independently; specify conversions, overflow and ABI where accepted |

## 6.24 — Runtime, memory and concurrency

| ID | Priority/type | Capability and dependencies | Completion evidence |
|---|---|---|---|
| OFIR-22 | G0/A | Ownership and destruction across partial moves, control flow and errors | Adversarial aggregate/resource tests; no double drop, use-after-free or leak |
| OFIR-23 | G0/A | Allocation size/alignment overflow, zero-sized values and OOM | Failure injection and consistent panic policy; fallible allocation requires an explicit design |
| OFIR-24 | G1/D | Interior mutability and shared ownership (5.9, 6.1) | Cell/UnsafeCell and Rc/Arc/Weak contracts, cycles and thread boundaries tested |
| OFIR-25 | G1/D | Threads, Send/Sync, atomics, locks, channels and thread-local state (6.1) | Memory ordering, deadlock/error behavior, joins and bounded-queue stress tests |
| OFIR-26 | G1/N | Cancellation, deadlines and shutdown | Reuse API-08/API-12; resources and workers terminate under failure |
| OFIR-27 | G1/A | Panic, abort, exit, backtraces and FFI failures | Preserve abort semantics; do not assume unwinding or cleanup after abort |
| OFIR-28 | G2/X | Custom allocators, arenas and allocation-aware collections | Measured workload benefit, fragmentation and ownership contracts |
| OFIR-29 | G2/X | Async/await, green tasks and runtime interoperability | ADR based on workloads; address cancellation and suspended-reference lifetimes |
| OFIR-30 | G0/A | Layout and ABI: niches, enums, zero-sized types, repr C and LLVM attributes | Cross-language layout/call fixtures and optimization correctness tests |

## 6.25 — Compiler engineering

| ID | Priority/type | Capability and dependencies | Completion evidence |
|---|---|---|---|
| OFIR-31 | G0/A | Frontend recovery and bounded handling of malformed input | Useful multiple diagnostics; adversarial/fuzz cases terminate |
| OFIR-32 | G0/A | AIR and lowering invariants | Validators detect malformed internal state before LLVM; actionable compiler failures |
| OFIR-33 | G0/A | Auditable seeds and bootstrap verification (6.9) | Stage 2/3 parity and shared-subset parity; stage 1 extensions have independent tests |
| OFIR-34 | G1/D | Incremental and parallel compilation (6.6) | Complete cache keys, dependency invalidation, corrupt-cache recovery and cold/warm measurements |
| OFIR-35 | G1/A | Optimization and code size (5.14, 5.15) | Track runtime, compile time, memory and monomorphization; preserve safety checks |
| OFIR-36 | G1/D | Debug information (5.13) | Real debugger stepping and locals across generics, optimized code and supported targets |
| OFIR-37 | G1/N | Compiler service interfaces for check/build/metadata | Structured diagnostics, stable exit status, cancellation and editor integration contracts |
| OFIR-38 | G2/X | Alternative backend, JIT or fast debug compilation | Profiled need, semantic parity and maintenance budget before adoption |

## 6.26 — General-purpose standard library

| ID | Priority/type | Capability and dependencies | Completion evidence |
|---|---|---|---|
| OFIR-39 | G1/A | Option/Result, comparison, hashing, cloning, defaults, formatting and slices | Generic user types work through interfaces; audit existing built-ins (5.3–5.6) |
| OFIR-40 | G1/N | Sets, deques, priority queues and ordered collections | Select by workload; complexity, invalidation and allocation contracts tested |
| OFIR-41 | G1/A | Bytes/text, UTF-8, builders, search and Unicode operations | Explicit byte/scalar/grapheme distinction; normalization, regex and locale are separate decisions |
| OFIR-42 | G1/A | Math, floating point, random distributions and precision-sensitive numbers | Numeric boundary tests; ordinary RNG distinct from API-09 cryptographic entropy |
| OFIR-43 | G1/D | Streams, filesystem, paths, processes, environment, clocks and dates | Reuse API-01–API-08; portable contracts and cleanup tests |
| OFIR-44 | G1/N | Serialization, byte order, encodings, checksums and compression | Reuse API-13–API-16; assess CSV/TOML/YAML/XML and archives independently |
| OFIR-45 | G1/N | CLI/terminal facilities | Arguments/help, completions, standard streams, error exits and no-color behavior tested |
| OFIR-46 | G1/D | Formatting and testing utilities | Assertions, fixtures and benchmarks match documented exports; resolve interface/closure dependencies |
| OFIR-47 | G1/N | Public library quality and versioning | Document allocation, panic/error behavior, complexity, examples, targets and ownership |

## 6.27 — Interoperability and targets

| ID | Priority/type | Capability and dependencies | Completion evidence |
|---|---|---|---|
| OFIR-48 | G0/A | C FFI: exports, callbacks, buffers, ownership, errno and threading | C fixtures prove ABI; decide variadics/unions and callback lifetimes explicitly |
| OFIR-49 | G1/N | Static/shared libraries, headers and binding generation | Build a C consumer; specify native dependencies, symbols and runtime allocation ownership |
| OFIR-50 | G1/N | Target support tiers and host/target distinction | CPU, ABI, endianness, minimum OS/toolchain and CI coverage stated; preserve current 64-bit scope |
| OFIR-51 | G1/N | Cross compilation | Sysroots, host tools, linker and target std/runtime resolved; execute artifacts on target |
| OFIR-52 | G1/A | Windows, macOS and Linux completeness (6.8) | Paths, encoding, processes, filesystem, networking, debugging and packages validated per tier |
| OFIR-53 | G2/X | WebAssembly/WASI and browser execution | Separate host contracts; browser DOM integration is not implied by a Wasm target |
| OFIR-54 | G2/X | Embedded/freestanding/no-std operation | Concrete hardware, startup, interrupts, MMIO, linker, panic and allocator budget |
| OFIR-55 | G2/X | C++, Python, JVM, .NET, JavaScript and plugin adapters | Select consumers; versioned C/component boundary and lifecycle tests |

## 6.28 — Developer experience

| ID | Priority/type | Capability and dependencies | Completion evidence |
|---|---|---|---|
| OFIR-56 | G1/D | Formatter (6.2) | Idempotence, comment preservation and semantic preservation |
| OFIR-57 | G1/D | Language server (6.3) | Diagnostics, completion, hover, navigation, rename, references and unsaved multi-package edits |
| OFIR-58 | G1/D | Diagnostics (5.12) | Stable codes, notes/help, structured output and tested fixes; registry stays synchronized |
| OFIR-59 | G1/N | Lints and static analysis | Suspicious/resource/deprecation checks with tested suppression and configuration scopes |
| OFIR-60 | G1/N | Debugger/editor integration | Launch/attach, breakpoints, stack and variables work with OFIR-36 |
| OFIR-61 | G1/N | Profiling and coverage | Source mapping through generics/inlining, export formats and measured overhead |
| OFIR-62 | G2/X | Watch mode, scratch files, REPL and notebooks | Prioritize quick edit/check/run; interactive execution needs explicit state semantics |
| OFIR-63 | G1/D | Documentation generation and doctests (6.4) | Search, cross-links, visibility, versioning, offline access and runnable examples |

Use the [Language Server Protocol](https://microsoft.github.io/language-server-protocol/)
and [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/overview.html)
as integration references. Syntax highlighting alone satisfies neither contract.

## 6.29 — Packages, builds and distribution

| ID | Priority/type | Capability and dependencies | Completion evidence |
|---|---|---|---|
| OFIR-64 | G1/D | Dependency resolution and locks (6.5) | Version conflicts, cycles, checksums and deterministic resolution tested |
| OFIR-65 | G1/N | Workspaces and dependency sources | Path/git/registry, dev/build/target dependencies, overrides, vendoring and offline contracts |
| OFIR-66 | G1/N | Build profiles, configuration and native/generated inputs | If hooks are adopted, specify host/target, declared inputs and network/permission policy |
| OFIR-67 | G1/N | Registry and publishing | Namespaces, authentication, immutable versions, yanking, mirrors and recovery |
| OFIR-68 | G1/N | Supply-chain integrity and provenance | Checksums, trust roots, advisories, license metadata and SBOM; reuse API-47 |
| OFIR-69 | G1/N | Installation and toolchain management | Clean-machine archives/install, pinned toolchains, checksums, rollback and uninstall |
| OFIR-70 | G0/A | Reproducible builds | Declare environment and artifacts; compare independent outputs including paths/timestamps |
| OFIR-71 | G1/N | Release engineering | Candidates, compatibility corpus, LLVM matrix, patch policy and installed-artifact tests |

Bootstrap IR parity and binary reproducibility are different gates. The latter
requires defined inputs/environment and bit-identical selected artifacts; see
the [reproducible-build definition](https://reproducible-builds.org/docs/definition/).

## 6.30 — Quality, performance and security

| ID | Priority/type | Capability and dependencies | Completion evidence |
|---|---|---|---|
| OFIR-72 | G0/A | Conformance and feature interaction tests | Positive/negative cases mapped to OFIR-01 across stages and target tiers |
| OFIR-73 | G0/A | Fuzzing, properties and differential testing | Extend existing tools with minimized regression seeds for parser, checker and optimizer |
| OFIR-74 | G0/A | Sanitizers, leaks, races and fault injection | Exercise unsafe/resource boundaries; report unavailable checks explicitly |
| OFIR-75 | G1/A | Performance budgets | Compile time/RSS, binary size, startup, cold/warm builds and runtime distributions; retain existing benchmark gate |
| OFIR-76 | G1/N | Representative real applications | Compiler, CLI/filesystem utility, C consumer, concurrent program and API service across releases |
| OFIR-77 | G1/A | Reliable, fast CI | Safe caching/sharding/parallelism, flake tracking and retained failure artifacts without lost coverage |
| OFIR-78 | G1/N | Compiler/build and hosted-tool security | Malformed-source budgets, path/command handling, cache integrity, secret handling and playground isolation |

## 6.31 — Ecosystem, learning and governance

| ID | Priority/type | Capability and dependencies | Completion evidence |
|---|---|---|---|
| OFIR-79 | G1/N | API/service ecosystem (6.11–6.21a) | Reuse all 54 API records and their foundation/local/production/extension gates |
| OFIR-80 | G1/D | Book, tutorials, website and playground (6.10) | Versioned installation, ownership/error explanations and examples pass on clean machines |
| OFIR-81 | G1/N | Maintenance and contribution process | Issue/review triage, release responsibility, security reporting and maintainer continuity |
| OFIR-82 | G1/N | Package discovery and ecosystem quality | Searchable docs/compatibility metadata, examples and criteria for maintained official packages |
| OFIR-83 | G2/X | GUI/TUI, graphics, audio and media | Package-level workload, event-loop, accessibility and platform requirements |
| OFIR-84 | G2/X | Scientific/data/ML computing | Arrays/tensors, SIMD/BLAS, dataframes and GPU only with concrete performance/memory goals |
| OFIR-85 | G2/X | Games, mobile and browser application SDKs | Bindings, deployment, cross-debugging, target support and named maintainers |
| OFIR-86 | G1/N | Scope and capacity governance | Priorities, owners, dependencies, estimates per small PR and explicit rejected alternatives |

## Architectural boundaries and non-goals

Preserve accepted ADRs: ownership without mandatory GC, errors as values, panic
abort, structural interfaces, monomorphization and compile-time reflection.
This inventory does not introduce inheritance, exceptions, unrestricted runtime
reflection, operator overloading or mandatory async. Reconsider a choice only
through a concrete proposal and ADR, with compatibility and maintenance costs.

Compiler features belong in the language only when libraries cannot express the
required semantics. Portable primitives belong in runtime/std; databases,
frameworks, GUI and vendor SDKs usually belong in versioned packages. A general
purpose language does not need to bundle every domain ecosystem.

For decisions, consult [Unicode identifier guidance](https://www.unicode.org/reports/tr31/)
without assuming Unicode identifiers must be adopted. The
[Rust undefined-behavior reference](https://doc.rust-lang.org/reference/behavior-considered-undefined.html)
illustrates the precision needed for unsafe contracts; it does not define Ofir's
semantics. [WASI](https://wasi.dev/) is an optional host-interface reference,
not a current Ofir target guarantee.

## Recommended sequence and release gates

1. Establish OFIR-01/02/03/07 and audit ownership, allocation, ABI, compiler
   invariants, bootstrap and existing quality gates. Classify unknowns honestly.
2. Audit the remaining stage 1 limits and open semantic decisions in the
   [roadmap](../roadmap.md), including LANG-01–20; preserve completed phase 5
   work and coordinate shared interfaces.
3. Advance general std, threads, editor tools, packages and installation alongside
   independent API foundations. Do not make all progress depend on HTTP or async.
4. Validate real CLI, C interoperability and concurrent programs as well as the
   API reference service. Feed missing semantics and ergonomics back into tasks.
5. Select the G1 release subset explicitly. Freeze contracts only after target
   coverage, compatibility, installation and maintenance evidence exists.
   G2 tracks need their own owners and acceptance decisions.

A release review should show the implementation matrix, supported target and
LLVM versions, bootstrap evidence, conformance/security results, performance
budgets, std/package compatibility, clean installation, migration guidance and
release ownership. Compiler architecture performance targets are goals until
measured; passing stage parity is not proof of all these properties.

Before activating a record, attach its existing roadmap item where applicable,
owner, spec/ADR decision, dependencies, stage/target scope and concrete tests.
Split work into independently reviewable PRs; estimate those PRs rather than
assigning a misleading size to this inventory. Keep the roadmap, specification,
standard-library reference and risk register consistent as decisions are made.
