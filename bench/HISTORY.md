# Benchmark history

Measurements of `bench/run_bench.py` at each `perf` commit and milestone.
Instruction counts are comparable across machines; wall times only on the
same machine.

Machine A: Intel Xeon @ 2.10GHz, 4 cores, Linux 6.18, gcc 13.3, release build (`-O2`).

## lex-100k (100 000 lines, 4.0 MB)

Up to `5abdd0c` the input had 871 037 tokens. From `e66fa35` the generator
parenthesizes nested binary expressions (886 577 tokens), so later numbers
are not comparable with earlier ones.

| Commit | Change | Instructions | Wall (ms) | Lines/s | Peak KiB |
|--------|--------|-------------:|----------:|--------:|---------:|
| `80c3480` | baseline: first stage 0 lexer | 652,978,395 | 103.3 | 0.97M | 37,600 |
| `71a2939` | keywords classified by the interner | 264,594,490 | 55.4 | 1.81M | 37,532 |
| `5abdd0c` | files read into the arena, lines indexed with memchr | 210,315,016 | 49.2 | 2.03M | 33,880 |
| `e66fa35` | new input (valid comparisons) | 211,833,128 | 51.2 | 1.95M | 34,440 |

Profile of the current version (callgrind, share of instructions): `lex`
35%, `emit` 13%, `intern` 7% + `sv_hash` 6%, `lex_number` 5%,
`lex_string_part` 4%, `strtod` 4%. Next candidates: reserving the token
list up front and a faster hash for short identifiers.

## parse-100k (100 000 lines, lex + parse)

| Commit | Change | Instructions | Wall (ms) | Lines/s | Peak KiB |
|--------|--------|-------------:|----------:|--------:|---------:|
| `e66fa35` | baseline: first stage 0 parser | 532,273,215 | 141.5 | 0.71M | 91,528 |
| `8ff66f0` | doc comment tokens dropped in place, not copied | 533,476,261 | 122.1 | 0.82M | 64,008 |
| `c37458a` | expressions grow from 48 to 56 bytes (TypeId annotation) | 533,596,957 | 117.4 | 0.85M | 70,808 |
| `6a46c6a` | precedence climbing: one table lookup per operator, not one call per level | 473,524,258 | 114.4 | 0.87M | 71,616 |

The parser itself costs about 320M instructions, 1.5 times the lexer.
Profile (callgrind, parse-10k, share of all instructions): `lex` 19%,
precedence climbing (`level` + `operator_at`) 9%, `arena_alloc` 5%, token
helpers (`at`, `peek`, `accept`) 8%, `malloc`/`free` 4% from the vectors
that collect list items. Next candidates: one table lookup per binary
operator instead of one call per precedence level, inlining the token
helpers, and building lists directly in the arena. Memory: 32-byte tokens
(28 MB at 100k lines) dominate the peak.

## check-100k (100 007 lines of the `typed` profile, lex + parse + check)

| Commit | Change | Instructions | Wall (ms) | Lines/s | Peak KiB |
|--------|--------|-------------:|----------:|--------:|---------:|
| `c37458a` | baseline: first resolver and type checker | 561,991,670 | 140.1 | 0.71M | 70,048 |
| `770ed99` | prelude `std/core` parsed and checked (ADR 0019) | 568,762,187 | 127.0 | 0.79M | 68,812 |
| `c78cce1` | generics (2.18); the common case of `coerce` inlined | 567,130,671 | 122.1 | 0.82M | 68,748 |
| `fbc80ef` | std bodies checked only when lowered; std grew (checked integers, `parse`) | 589,025,975 | 137.3 | 0.73M | 68,924 |
| `6a46c6a` | precedence climbing in the parser | 525,711,881 | 134.7 | 0.74M | 68,880 |
| `bce26e1` | `--emit=names` trace checks in the resolver (+1.8%, within the gate) | 535,002,000 | 125.5 | 0.80M | 68,924 |

On the same input, stopping after the parser costs 489.4M instructions:
resolution and type checking add 72.6M (13% of the total, a third of the
lexer).

## llvm-100k (100 007 lines of the `typed` profile, up to the LLVM IR, no clang)

| Commit | Change | Instructions | Wall (ms) | Lines/s | Peak KiB |
|--------|--------|-------------:|----------:|--------:|---------:|
| `5c273fa` | baseline: first AIR lowering and LLVM IR generator | 4,091,654,779 | 780.4 | 0.13M | 226,072 |
| `f0a5e18` | buffered writer instead of printf | 2,382,338,156 | — | — | 226,072 |
| `5f311c6` | one function lowered and generated at a time | 2,377,803,874 | 442.9 | 0.23M | 81,500 |
| `770ed99` | prelude `std/core`; only functions reachable from `main` lowered | 2,694,652,488 | 463.9 | 0.22M | 68,800 |
| `c78cce1` | generics (2.18): calls walked once more to reach instances | 2,705,084,632 | 495.7 | 0.20M | 68,776 |

The prelude (690 lines of `std/core`) costs a fixed 2.6M instructions per
compilation: +4.4% on check-10k, +0.6% on check-100k, an accepted
regression (ADR 0019). Most of it is the integer methods (now `std/core/num/`), one copy per
integer type; generic bounds (5.2) will reduce it to one declaration.

Warnings (2.24), the move analysis (3.1) and drops (3.2), measured together
against `df84bf9` on the inputs whose variables start with `_` (from
`2ae90d5`, so that they report no warnings): check-10k 65,654,385 (+2.5%),
check-100k 583,294,154 (+2.1%), llvm-10k 286,589,724 (+1.9%), llvm-100k
2,790,594,623 (+1.7%). The move analysis first cost +13.8%; it now runs
only in functions that move a value that is not trivially Copy, std
functions only when the lowering reaches them, loops are walked once when
nothing changes, and the analysis reuses a scratch arena.

`List<T>` in the prelude (`std/core/list.of`, 2.20) adds 0.7M instructions
to every compilation: +1.7% on check-10k, +0.2% on check-100k.

Until `2da11c2`, `make bench-compare` ran the base compiler without the
prelude (it looked for `std` in the wrong place), which overstated every
regression measured against a base that has one by about 2.6M
instructions. Only the
std functions a program reaches are lowered, so nothing reaches LLVM unless
it is used.

The baseline spent about 65% of its instructions in the C library's printf.
The generated IR is 66 MB for 100k lines, about 23 IR lines per source
line: next candidates are fewer loads and stores (keeping temporaries in
SSA values instead of allocas) and deduplicated string constants, which
would also make clang faster (16 s at -O0 for this input).

Binary size of `examples/hello.of`: 17,440 bytes, 14,584 stripped.

## Generated code against C (roadmap 3.11a)

`bench/run_programs.py`: each program of `bench/programs` built with
`ofir build --release` against its C version in `bench/baselines` built
with `clang -O2`, median of 3 runs, machine A, clang 18. The outputs agree.

| Commit | Program | Ofir (ms) | C (ms) | Ofir / C |
|--------|---------|-----------:|-------:|----------:|
| `0e0f46c` | `binary_trees` (depth 18) | 2,736.7 | 2,707.7 | 1.01 |
| `0e0f46c` | `fib` (35) | 59.7 | 33.1 | 1.80 |
| `0e0f46c` | `nbody` (5M steps) | 326.1 | 313.7 | 1.04 |

`fib` pays for the overflow checks of `+` and `-` (spec 2.2): they keep
LLVM from turning one of the two recursive calls into a loop, as it does
for C. Written with `wrappingAdd` and `wrappingSub`, the same program
takes 32.6 ms, as fast as C. Allocation (`binary_trees`, one `malloc`
per box on both sides) and floating point with bounds-checked list
indexing (`nbody`) are within 5% of C.

## Generated code against C, Rust and Go (roadmap 5.15)

PR #13, commit `47e2e34`, GitHub Actions `ubuntu-24.04` runner
(`Linux-7.0.0-1017-azure-x86_64-with-glibc2.39`): one warmup and median of
three timed runs. The three programs' outputs agreed across all four languages
before and during timing.
Toolchains: Ofir 0.1.0 (stage 0), clang 17.0.6, rustc 1.99.1, Go 1.24.13.
Wall times include process startup; ratios are relative to C in this run.

| Program | Ofir (ms) | C (ms) | Rust (ms) | Go (ms) | Ofir / C |
|---------|----------:|-------:|----------:|--------:|---------:|
| `binary_trees` | 1,510.249 | 2,000.586 | 1,396.399 | 2,850.029 | 0.755 |
| `fib` | 33.140 | 19.215 | 19.188 | 35.185 | 1.725 |
| `nbody` | 219.814 | 226.128 | 226.372 | 318.723 | 0.972 |

This is a CI snapshot, not a release measurement. At each release, append
results from a fixed machine with the toolchain versions and run count.

## Stage 1 object-cache hashing (local Windows measurement)

Self-build of `compiler/main.of`, warm cache, 32 logical CPUs, clang with
the MinGW target, debug profile. `--time-passes` showed that computing the
SHA-256 keys of all codegen units took 3,892 ms before moving this cache
hash to the C runtime, and a median of 116 ms after (five runs). The
complete build took 12,633 and 12,742 ms before, and a median of 8,880 ms
after (five runs). These runs are on one machine; the cache tests and hash
vectors passed, but a cross-platform benchmark is needed before treating
the wall-time ratio as a general result.

## Optimized stage 1 compiler (local Windows measurement)

Self-build of `compiler/main.of`, warm object cache, 32 logical CPUs, clang
targeting MinGW. Building stage 1 with stage 0 `--release` took 44.34 s,
versus 15.32 s without it. The resulting stage 1 compiled the same input
in a median of 1,789 ms, versus 7,786 ms for the unoptimized stage 1
(three warm runs each). The two compilers emitted byte-identical LLVM IR
for their own source. The extra one-time build cost is recovered after
about five such compilations. An unchanged second `make stage1` took 1.42 s
with the new input-based build rule. These are local results, not a CI
performance baseline.

## Stage 1 LLVM function buffer reuse (local Windows measurement)

The stage 1 compiler reuses its function IR string's capacity between bodies
instead of starting with an empty string each time. On a warm self-build of
`compiler/main.of`, alternating the old and new release compilers gave median
codegen times of 621 ms and 607 ms, and median total times of 1,913 ms and
1,884 ms (four old and five new runs, excluding the first cold build).
Both compilers emitted byte-identical LLVM IR for that source. These are
local timings; the improvement is small enough to recheck on other hosts.

## Compile cost of the program benchmarks (roadmap 6.25e)

Stage 0 release compiler, Linux, 4 cores, clang 18, one run, commit on
branch `feat/ofir35`. Compile time is a `--release` build in an empty object
cache; memory is the peak of the build (clang's); size is the executable.

| program | compile ms | build MB | IR functions | Ofir KB | C KB | Rust KB | Go KB |
|---------|-----------:|---------:|-------------:|--------:|-----:|--------:|------:|
| binary_trees | 115 | 81 | 36 | 42.8 | 15.8 | 4259 | 2175 |
| fib | 108 | 81 | 31 | 38.7 | 15.7 | 4257 | 2157 |
| map_count | 168 | 82 | 68 | 42.7 | 15.8 | 4278 | 2157 |
| nbody | 144 | 82 | 47 | 42.8 | 15.6 | 4284 | 2175 |
| sort | 151 | 82 | 45 | 42.7 | 15.8 | 4265 | 2172 |
| strings | 171 | 82 | 59 | 43.0 | 15.9 | 4260 | 2174 |

Ofir executables, unstripped and with the runtime linked statically, stay
under the 50 KB target of `docs/design/performance.md`.

