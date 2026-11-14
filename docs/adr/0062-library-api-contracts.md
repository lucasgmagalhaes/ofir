# 0062. The documented contract of public library APIs

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-47 asks that public library APIs document allocation, panic and error
behavior, complexity, examples, targets and ownership, and that they say
how they are versioned.

- ADR 0020 fixed the tags of a documentation comment (`@param`,
  `@returns`, `@panics`, `@safety`, `@example`, `@see`, `@deprecated`),
  and `tools/check_docs.py` checks them against each signature.
- Nothing says how fast an operation is, when it allocates or which
  targets behave differently, so the collections state complexity only in
  prose and the system modules only in docs/std.
- 46 of the 55 std modules have no example in their module comment.
- ADR 0037 already says how std is versioned: with the compiler, with no
  promise before 1.0, and with `@deprecated` for one minor release before
  a removal.

## Decision

1. **Three new tags.**
   - `@complexity` gives the time of an operation in terms of its inputs,
     such as `O(log n)` or `O(1) amortized`; `n` is the number of
     elements unless the text says otherwise.
   - `@allocates` says when an operation allocates and what, when that is
     not plain from the return type (a returned `str` or `List` is always
     a new allocation).
   - `@platform` says how an operation differs between targets, or that
     it exists only on some.
   `ofir doc` gives each a section ("Complexity", "Allocates",
   "Platform"), as it does for `@panics`.
2. **What every public item states.**
   - Ownership is in the signature: parameters are borrowed unless they
     are `move`, and returns are owned. A function that returns a view of
     an argument says so in `@returns`.
   - Panics are for broken preconditions of the caller, and each one is in
     `@panics`. Failures of the environment (files, processes, input) and
     invalid input return `Result` or `Option`, and `@returns` names the
     error.
   - Every method of the collections in `std.collections`, and of `List`
     and `Map`, has `@complexity`.
   - Every module comment outside `std.core`, which the prelude documents
     (docs/std/core.md), has an `@example`; doc tests run it in stage 1.
3. **`tools/check_docs.py` enforces 2.** It knows the new tags, and it
   reports a module comment without `@example` and a collection method
   without `@complexity`.
4. **Versioning stays ADR 0037.** std is versioned with the compiler; the
   new tags do not add a `@since`, which waits for 1.0.

The contract is written for users in docs/std/README.md.

## Consequences

- A reader learns the cost and failure modes of a call from its comment
  and from `ofir doc`, not from the source.
- New std modules and collection methods cost an example and a complexity
  line, enforced by CI.
- `std.core` modules may still lack module examples; the prelude page
  covers them, and their functions have their own.
