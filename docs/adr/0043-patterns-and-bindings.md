# 0043. Patterns, bindings and labeled control flow

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-18 asks to settle the open pattern questions of the roadmap (open
question 2: slices, strings, `name @ pattern`) with examples and
diagnostics before the syntax grows. The state before this ADR:

- **Strings.** String literal patterns, alone and in alternatives
  (`"get" | "head"`), already compile and run in both stages.
- **Guards.** `pattern if cond` works on every pattern kind, tuples
  included; exhaustiveness ignores guarded arms.
- **Labeled control flow.** `name: for` and `break name` / `continue name`
  are settled (open question 3) and implemented in stage 1.
- **`name @ pattern`.** Missing: binding a value and testing it at once
  needed a guard (`n if n >= 0 && n <= 9`) that exhaustiveness cannot see,
  or a second match on the whole value.
- **Slices.** Missing. A slice pattern (`[first, ..rest]`) binds `rest` as
  a borrow of part of the slice, which depends on the slice borrow rules
  still open in OFIR-19 (mutable slices, reborrows).

## Decision

- **Adopt `name @ pattern`** (spec 4.5). The pattern after `@` takes the
  alternatives that follow: `x @ 10 | 20` is `x @ (10 | 20)`, so the
  binding never has to repeat in each alternative. `mut name @ pattern`
  makes the binding mutable. It is valid wherever a pattern is: arms,
  `if const`, `const`, variant arguments and struct fields.
- **The inner pattern binds nothing** (E0502). Binding the whole value and
  its parts at once would bind the same value twice, which needs
  by-reference bindings Ofir does not have. Parts are read through the
  name, or bound without `@`.
- **The binding behaves like a plain binding** for ownership: an owned
  value moves into it, a borrowed place is borrowed, a Copy value is
  copied. Exhaustiveness uses the inner pattern.
- **Strings** stay as they are; the spec now names them.
- **Slice patterns are deferred** until OFIR-19 settles slice borrows.
- **Labeled blocks with values** (`name: { ... break name value }`) are not
  adopted: labeled loops and `match` cover the cases seen so far.
- **Guards** are unchanged.

## Consequences

- Range and variant tests can name their value without a guard, and the
  checker still proves the match exhaustive (`small @ 0..=9`, `big @ _`).
- `@` after a name on the same line is a binding, so `ofir fmt` spaces it
  (`n @ 1..=9`); an attribute `@name` keeps its tight form.
- Stage 1 only: stage 0 does not parse `@` in patterns, and the compiler
  sources do not use it.
- Open question 2 keeps only slices, tied to OFIR-19.
