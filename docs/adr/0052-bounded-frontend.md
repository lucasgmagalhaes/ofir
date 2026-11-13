# 0052. A nesting limit bounds the frontend

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-31 asks for frontend recovery and bounded handling of malformed
input: useful multiple diagnostics, and adversarial or fuzz input that
terminates.

Recovery already gave useful diagnostics: the parser reports one error
per statement and resynchronizes at the next one, and the checker
reports every type error. Semantic recursion is bounded too: generic
instantiation (E0331), recursive types (E0318), alias and constant
cycles (E0310, E0800) and comptime calls (E0816).

Syntactic depth was not. Every phase walks the tree recursively, and
stage 1, built with `--release`, has large frames (`parsePrimary` takes
8 KiB, `lowerBlock` 6 KiB). With the 8 MiB stack, stage 1 crashed with
"stack overflow" on about 500 nested blocks, 700 parentheses or a chain
of 700 `+`, the last in AIR lowering; stage 0 crashed at 10000.

## Decision

1. **The parser limits nesting to 256 levels (E0101), in both stages.**
   A level is a nested expression, block, type or pattern, or one step of
   a chain: a binary operator, an `as`, a postfix operation or an
   `else if`. Chains count because they build a tree as deep as they are
   long, and the later phases recurse on it.
2. **Past the limit the parse stops.** The parser reports E0101 once, at
   the token where the limit is passed, moves to the end of the file and
   unwinds; no other syntax error follows. As with any syntax error,
   names and types are not checked.
3. **The error count is not capped.** Output stays linear in the input:
   at most one syntax error per statement.
4. **Stage 1 keeps stage 0's limit and counting**, so both report the
   same diagnostics (`tools/compare_stages.py`).

## Consequences

- No input makes the compiler overflow its stack through syntactic
  depth. At the limit, both stages compile in about 0.1 s.
- 256 levels are far beyond written code; generated code that goes past
  them must use intermediate bindings.
- The limit is part of the language (spec A.8): a program accepted by
  one compiler is accepted by the other.
