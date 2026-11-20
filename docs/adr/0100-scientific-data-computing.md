# 0100. Scientific and data computing are packages over f64 buffers

- Status: Proposed
- Date: 2026-11-20

## Context

OFIR-84 (roadmap 6.31g) covers scientific, data and machine learning
computing: arrays and tensors, SIMD and BLAS, dataframes and GPUs, each
only with concrete performance and memory goals. It is a G2 decision:
it needs a workload and an owner, and it may be rejected.

What exists:

- `f32` and `f64` with the elementary functions of C's math library,
  `std.math.sum` with compensation, `approxEq`, and the seeded
  distributions of `std.random` (ADR 0057).
- `List<T>` stores its elements contiguously; `&[T]` and `&mut [T]`
  view them, and `asPtr` and `asMutPtr` give a C library the address of
  the first element.
- The C boundary (ADR 0063, 0070): `extern "C"` functions, raw pointers
  and `@link`. BLAS, LAPACK, FFTW, the Arrow C data interface and GPU
  runtimes are all reached through a C ABI.
- Tasks on OS threads (ADR 0050) for parallel loops.
- No SIMD vector type, no multi-dimensional array, and no workload or
  owner for a tensor, a dataframe or a GPU kernel.

Numerical libraries share one shape: a matrix is a pointer to
contiguous elements, its sizes and a leading dimension (the distance
between rows), and the caller owns the memory. The question for a
language is whether its own loops over such a buffer are fast enough
that only the large kernels (a tuned matrix product, an FFT, a GPU
kernel) need a library.

## Decision

1. **No tensor, dataframe, linear algebra or GPU module in `std`** in
   version 1. Each is a package that binds a C library, in the package
   index (ADR 0081, 0097), owned by whoever needs it.
2. **The numerical buffer is a `List<f64>` (or `f32`) in row-major
   order**, passed to C as a pointer, its sizes and a leading dimension.
   `tests/run/ffi/numeric_kernel.of` is the evidence: an Ofir product
   over `&[f64]` and `&mut [f64]` and a C kernel with the signature
   shape of `cblas_dgemm` give the same matrix, and the kernel writes a
   block of a wider buffer through its leading dimension. The buffer
   stays owned by Ofir; C must not keep the pointer after the call,
   since the list moves its elements when it grows.
3. **The performance goal is the scalar loop.** A loop over `f64`
   buffers, bounds checks on, stays within 10% of the same loop in C
   built with `clang -O2` (the target of `bench/README.md`).
   `bench/programs/matmul.of`, a 500 x 500 product with C, Rust and Go
   baselines, measures it in `make bench-programs`, and
   `bench/budgets.json` gives it a ceiling of 485 M instructions in
   `make check-budgets`.
4. **Deferred until a workload is named:** SIMD vector types and
   intrinsics, a multi-dimensional array type with strided views,
   `f16` and `bf16`, a dataframe and a GPU target. Each needs a kernel
   that the scalar loop misses by more than the goal of decision 3, or
   memory it cannot fit.
5. **The conditions to reopen** a module or a type for one of these: a
   named program and owner, the kernel and its measured goal (time or
   memory against C or a named library), the targets it needs (tier 1
   at least), and a test that runs in CI without a GPU.

## Consequences

- A package that binds BLAS or LAPACK writes an `extern "C"` block over
  pointers and sizes; the C boundary needs nothing new for it.
- A matrix type is the package's work: a struct over a `List<f64>`, its
  rows, columns and leading dimension.
- A regression in the code generated for float loops shows up in the
  `matmul` budget.
- GUI, graphics and media stay with ADR 0099; browser and mobile SDKs
  with OFIR-85.
