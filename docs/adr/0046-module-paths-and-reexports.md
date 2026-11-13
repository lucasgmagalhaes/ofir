# 0046. Module paths, re-exports and initialization

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-20 asks for modules, nested paths, visibility, re-exports and
dependency cycles, with cross-module resolution and the initialization
policy tested. Before this ADR:

- A path had at most two segments (`module.Name`); `a.b.Name` in a type was
  E0901 and in an expression did not resolve.
- Modules could not re-export: a plain `import` is private, and there was
  no way to build a facade module.
- Circular imports already worked, and module-level `const` and `let`
  initializers were already compile-time only (E0801), with E0800 for a
  constant that depends on itself, across modules too.

## Decision

- **`pub import path [as name]` re-exports.** The imported module or
  declaration becomes a public member of the importing module under its
  last segment or alias, and also binds locally like `import`. A
  re-export never warns as unused.
- **Only public declarations can be re-exported** (E0202). Re-exports chain;
  the resolver binds them in dependency order before resolving items, and
  a re-export cycle that reaches no declaration names nothing (E0200).
- **Nested paths resolve segment by segment.** In `a.b.c.Name`, each module
  segment must be a public member of the previous module: a re-exported
  module. A private member is E0202; a non-module in the middle is E0200.
- **A submodule is not an implicit member of its parent.** Only imported
  modules are loaded, so `parser.lexer` is reachable only through
  `import parser.lexer` or a `pub import` in `parser`. E0200 for a
  submodule that exists names both fixes in a help.
- **Initialization is static.** Modules run no code at startup; every
  module-level initializer is a compile-time value. Load order is not
  observable and import cycles are always safe; self-dependent constants
  stay E0800.

## Consequences

- Facade modules (`index.of` re-exporting its submodules) are possible
  without wrapper functions.
- Implemented in stage 1 only; stage 0 keeps two-segment paths and has no
  re-exports, so stage 1 sources do not use them.
- Lazy or ordered module initialization (static constructors) stays out of
  the language; a program that needs one calls an `init` function from
  `main`.
