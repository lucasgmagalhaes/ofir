# 0057. Float math from libm, `std.math` helpers, a seeded `std.random`

- Status: Accepted
- Date: 2026-11-13

## Context

OFIR-42 asks for math, floating point, random distributions and
precision-sensitive numbers, with numeric boundary tests, and for an
ordinary random generator that is kept apart from the cryptographic
entropy of API-09. Today:

- `f32` and `f64` have `sqrt`, `abs`, `floor`, `ceil`, `min`, `max`,
  `isNan` and `parse`.
- There is no trigonometry, exponent or logarithm.
- There are no float constants such as `PI`, `INFINITY` or `EPSILON`.
- There is no random number generator.

Every program already links C's math library (`-lm`).

## Decision

1. **Elementary functions are methods on `f32` and `f64`.**
   - They call C's math library: `sin`, `cos`, `tan`, `asin`, `acos`,
     `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `exp`, `exp2`, `ln`, `log2`,
     `log10`, `powf`, `powi`, `cbrt`, `hypot`, `round`, `trunc` and
     `mulAdd`. `f32` calls the `f`-suffixed C function, so it never
     widens to `f64`.
   - `fract`, `copySign`, `signum`, `clamp`, `isFinite`, `isInfinite`
     and `isSignNegative` are written in Ofir.
   - These follow IEEE 754 and C: they never panic, and they give NaN or
     an infinity outside their domain.
   - Accuracy is C's. Only `sqrt`, `abs`, `floor`, `ceil`, `round`,
     `trunc`, `fract`, `copySign` and `mulAdd` are exact.
2. **`std.math` holds constants and helpers that are not methods.**
   - Constants: `PI`, `TAU`, `E`, `SQRT_2`, `LN_2`, `LN_10`, `INFINITY`,
     `NEG_INFINITY`, `NAN`, `EPSILON`, `MAX` and `MIN_POSITIVE`, all
     `f64`.
   - `approxEq(a, b, relTol, absTol)`, for comparisons that tolerate
     rounding.
   - `sum(values: &[f64])`, with Neumaier compensation, so that the
     result does not depend on the rounding of each step.
   - `gcd`, `lcm` and `isqrt` on `int`.
3. **`std.random` is an ordinary, seeded generator.**
   - `Rng` is xoshiro256\*\*, seeded through SplitMix64. The same seed
     gives the same sequence on every platform and in both stages.
   - `Rng.seeded(seed)` is reproducible. `Rng.fromTime()` takes its seed
     from the clock and is documented as unpredictable only by accident.
   - `nextU64`, `nextInt(lo, hi)` without modulo bias, `nextF64` in
     `[0, 1)`, `nextBool`, `chance(p)`.
   - Distributions: `normal(mean, stdDev)` (Box-Muller) and
     `exponential(rate)`.
   - On lists: `shuffle` (Fisher-Yates) and `choose`.
4. **`std.random` is not for secrets.** Keys, tokens and nonces need the
   operating system's entropy, which is API-09 and a separate module.
   The docs of `Rng` say so.
5. **Decimal and arbitrary precision are separate decisions.** A decimal
   type for money and a big integer each need their own design. Until
   then, `std.math.sum` and `approxEq` are the precision tools in std.

## Consequences

- Float results depend on the C library for the functions that are not
  exact. Tests compare them with `approxEq`, not `==`.
- Simulations and tests can replay a run from its seed.
- Float constants live in `std.math`, not on the type (`F.NAN`), because
  the compiler gives only integers associated constants.
- Programs that need secure randomness wait for API-09.
