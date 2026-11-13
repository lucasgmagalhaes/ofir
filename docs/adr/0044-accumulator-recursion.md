# 0044. Accumulator recursion with a deferred overflow check

- Status: Accepted
- Date: 2026-11-13

## Context

`fib` in `bench/programs` runs about twice as slow as the same C program,
though both are a recursion `fib(n - 1) + fib(n - 2)` with the same calls.
The difference is the overflow check of `+` (spec 2.2). Without it, LLVM
turns `return x + f(args)` into a loop with an accumulator (tail recursion
elimination with an associative accumulator), so one of the two calls
becomes a jump. With it, each addition must fail where it happens, so the
call stays a call and its result must come back before the add.

Turning off the check would change the language. The question is whether
the same loop can be had with the same semantics.

## Decision

A pass after the check folding (`air/accumulate.c`, `compiler/air/accumulate.of`)
rewrites one shape of function. The function must:

- return an integer type `T`, have parameters, and not be `main`, glue or
  a closure;
- have only scalar locals (integers, floats, `bool`, `()`), no `mut`
  parameters, no address taken and no field paths, and only simple
  rvalues (use, binary, checked, unary, casts, saturation) besides calls;
- have exactly one *site*: a block, not the entry, ending in
  `_c = call f(args)` (a call of itself, with the same type arguments),
  `_r = checked add x, _c` (or `_c, x`) and the overflow assertion, whose
  result reaches a return through copies and gotos;
- reach a return from the entry without passing through the site (a base
  case); a recursion without one keeps its calls and ends in a stack
  overflow as before.

`x` must be a constant that is not negative or a local. The pass adds an
accumulator `acc` of the unsigned type of the width of `T` and a sticky
`carry`. The site adds `x` to `acc` (setting `carry` on overflow), sets the
parameters to the arguments and jumps back to the old entry. Each return
of a value `v` checks `carry || acc > MAX - v` and returns `acc + v`.

**Why it is exact.** The original evaluates `x1 + (x2 + ... + (xm + v))`
from the innermost call out and fails at the first partial sum above
`MAX`. With every `xi >= 0`, each partial sum is at most the next one, so
some partial sum fails exactly when the whole one does: `P + v > MAX`,
`P` being the exact sum of the terms. `acc` holds `P` modulo `2^w` and
`carry` records that it wrapped, so the check is exact for `v` of any sign.
A local `x` of a signed type may be negative: the site then branches on
`x < 0` and takes the original call and add, whose own check stays where
it was. The calls keep their order, so the effects of the other calls do
too; the panic message and location are those of the site's addition.

## Consequences

- `fib(35)`: 45 ms to 32 ms with `--release`; C takes 23 ms and Rust 27 ms
  on the same machine. The remaining gap is the check of `n - 1` and
  `n - 2`.
- A recursion that would overflow the stack may finish, since the call
  that became a jump takes no stack. The stack is a resource, not part of
  the semantics (spec 6.5).
- Both stages run the pass, so stage 1 keeps the same blocks and locals in
  the same order (`compare-stages` covers `--emit=air`).
- Other shapes (`x * f(args)`, two sites, a struct parameter) keep their
  calls. They can be added in the same way when a program needs them.
