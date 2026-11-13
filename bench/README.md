# bench — benchmarks

Performance programs (`programs/`) with equivalent C, Rust and Go versions
(`baselines/`). Target: within 10% of C compiled with `clang -O2`.

Run `make bench-programs` on Linux or macOS. It builds the release compiler and
runtime, then builds each Ofir program and its C (`clang -O2`), Rust
(`rustc -C opt-level=2`) and Go (`go build`) baselines. The runner checks
outputs against C before measuring, performs one warmup and five timed rounds,
then writes medians and toolchain versions to
`build/generated-code-results.json`. It fails if any output differs.
Besides run time, it records each executable's size and, for Ofir, the
release compile time in an empty object cache, the peak memory of that build
(compiler or clang, whichever is larger) and the number of functions in the
optimized LLVM IR, so a change that multiplies monomorphized instances shows
up. Safety checks are not traded for speed: `tests/panic/release_*.of` checks
that `--release` keeps overflow, bounds, division and `as` range checks.

For a quick baseline check without the Ofir compiler, use
`python3 bench/run_programs.py --baselines-only --clang gcc --runs 1 --warmups 0`.
This checks C, Rust and Go output parity; its timings are not release results.
CI runs the complete suite with three rounds and uploads the JSON artifact.

At each release, run `make bench-programs` on a fixed machine and add the
result table, commit, machine, compiler versions and run count to `HISTORY.md`.
Compare ratios only when the workloads and toolchain conditions match.
