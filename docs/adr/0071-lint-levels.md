# 0071. Lint categories with levels per item, package and command

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-59 asks for suspicious, resource and deprecation checks, with
suppression and configuration scopes that are tested.

Ofir already has warnings (spec 12.6): unused variables and imports
(W0001, W0002), unreachable code (W0004) and deprecated items (W0006).
`@allow(category)` silences a category inside one declaration, and
`--deny-warnings` turns every warning into an error. Missing:

- a level for a whole package, kept in the project instead of on each
  declaration or command line;
- a check for code that is valid but almost certainly a mistake.

## Decision

1. **Categories.** A lint category names a set of warning codes:
   `unused` (W0001, W0002, W0003), `unreachable` (W0004), `deprecated`
   (W0006), `naming` (W0007) and `suspicious` (W0008). `@allow` and the
   manifest use the same names.
2. **Levels.** A category is `allow` (silenced), `warn` (the default) or
   `deny` (reported as an error).
3. **Scopes**, from the widest:
   - the command: `--deny-warnings` denies every warning;
   - the package: `"lints": {"suspicious": "deny"}` in `ofir.json`
     applies to the modules, test files and doc tests of that package;
     a dependency's manifest applies to its own modules only;
   - the declaration: `@allow(category)` silences the category inside it,
     whatever the package level.
4. **Suspicious code (W0008).** Comparing a place with itself (`x == x`,
   `a.b < a.b`) and assigning a place to itself (`x = x`). A place is a
   local, `self`, or a field or tuple field of one. Floating-point
   comparisons are not reported: `x != x` is the test for NaN.
5. **Resource checks** keep their errors: an ignored `Result` is E0305, a
   value used after a move is an error, and drops are exact (spec 6.3).
   A must-use attribute for other types waits until a library needs one.
6. **Deprecation** is W0006 with the category `deprecated`.
7. Stage 1 implements the levels and W0008. Stage 0 ignores the `lints`
   table and does not report W0008.

## Consequences

- A project can deny a category in CI without changing its code, and a
  declaration can still opt out.
- A new category needs a name in `diag.lintCodes`, a registry row and an
  explanation file.
- W0008 only sees places written the same way: `a[i] == a[i]` and calls
  are not reported, because their values can differ.
