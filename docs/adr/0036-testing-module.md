# 0036. Asserts and `Bencher` in `std.testing`, not in the prelude

- Status: Accepted
- Date: 2026-11-12

## Context

`assert(condition)` is a built-in function of the prelude (spec 7.5) in
both compilers, so every module sees it without an import. Spec 7.5 also
lists `assertEq`, which neither compiler implements. `Bencher` (spec 12.3)
is in the prelude too, but only under `ofir bench`, which loads
`std.core.bench` into every program it builds.

Test helpers are the only names of the prelude that exist for tests. A
test file cannot show where they come from, and every module, test or not,
sees them. Attributes are different: `@test`, `@inline` or `@repr(C)` are
syntax that the compiler checks against a fixed table, and they have no
declaration to import.

## Decision

- A new module, `std.testing` (`std/testing/index.of`), holds the test
  helpers:

  ```ofir
  extern "intrinsic" {
      pub fn assert(condition: bool)
      pub fn assertEq<T: Eq + Debug>(left: T, right: T)
      pub fn assertNe<T: Eq + Debug>(left: T, right: T)
  }
  ```

  and `Bencher`, moved from `std.core.bench`.
- They are not in the prelude. A module, test file or doc test that calls
  them imports them, as any other std name: `import std.testing.assert`.
  Without the import, `assert` is an unknown name (E0200), whose help
  names the import.
- The asserts stay intrinsics: the compiler recognizes them by name in
  `std.testing` and lowers each call where it is written, as it lowers
  `assert` today. A failed assert reports the location of the call, not a
  line of `std.testing`, and costs one branch, as before.
- `assertEq` and `assertNe` compare with `==` (`Eq`) and, when the assert
  fails, print both values with `Debug`:

  ```
  panic: assertion failed: left == right
    left: 3
   right: 4
    at src/calc_test.of:7:5
  ```

  They need interfaces, so only stage 1 has them; stage 0 has `assert`.
- `ofir bench` no longer adds `Bencher` to the prelude: a `@bench`
  function's module imports `std.testing.Bencher`.
- Attributes, `@test` and `@bench` included, stay part of the language and
  need no import.

## Consequences

- Every module that calls `assert` gains an import: the tests, the std
  and compiler modules that check invariants with it, and the doc tests of
  std, whose ```` ```ofir ```` blocks already see only what they import
  (spec 12.5).
- No run-time or compile-time cost: the calls lower as before, and
  `std.testing` is a small module, loaded only by the programs that import
  it.
- The prelude keeps `print`, `println`, `eprint`, `eprintln`, `panic`,
  `sizeOf` and `alignOf`.
- More helpers (`assertMatches`, approximate float comparisons) can join
  `std.testing` without growing the prelude.
