# 0033. Incremental and parallel builds: codegen units and an object cache

- Status: Accepted
- Date: 2026-11-07

## Context

Every build compiles the whole program from source, and generics are
monomorphized (ADR 0007). Stage 1 writes one LLVM module and gives it to
one clang process. Measured on stage 1 compiling itself (about 47,000
lines with std, a 4-core machine):

| Step | Time |
|------|------|
| resolve and type check | 3.1 s |
| lower to AIR, monomorphize | 2.3 s |
| generate the `.ll` (17 MB, 3,084 functions) | 4.1 s |
| clang `-O0` on that one file | 19 s |
| clang `-O2` on that one file | 60 s |

clang takes about two thirds of a debug build and about seven eighths of a
release build. It runs on one core while three are idle, and it compiles
every function again after any edit. The front end, at about 5,000 lines
per second, is also far from the 200,000 lines per second target
(`compiler-architecture.md`, section 14), but it costs a third of what
clang does.

There are two ways to make builds incremental:

- **Cache the front end.** Store the typed and lowered modules, keyed by
  content, and redo only the modules that changed and their dependents.
  This is how rustc's query system works. It needs a serialized form of
  the checker's tables, and an exact account of what each module reads
  from other modules: types, layouts, the instances of generics it
  requests. It is a large change that touches every phase.
- **Cache the back end.** Split the generated code into units, compile
  each unit to an object file of its own, and reuse the object of every
  unit whose text did not change. The front end still runs in full. The
  cache key is the unit's text, so the cache is correct whatever the
  front end does.

Parallelism can also go in two places: the front end (checking modules on
several threads) or the back end (several clang processes). The compiler
is written in the bootstrap subset, which has no closures, so it cannot
use `std.task` (`docs/bootstrap.md`). Running processes side by side only
needs a way to start a process without waiting for it.

## Decision

**Codegen units.** `build`, `run` and `test` split the generated code into
**units**, one for each source module. A unit holds:

- the functions of its module;
- the instances of generic functions and methods that its module's code
  requests, wherever the generic is defined;
- the drop and clone glue, closure thunks and vtables that its code uses;
- its own string and location constants, and the type definitions and
  declarations it refers to.

The program's `main` and the test table go in the unit of the root
module.

**Linkage.** A function that is called only from its own unit stays
`internal`. A function called from another unit becomes external with
`hidden` visibility and keeps `fastcc`; the other units declare it.
Instances, glue, thunks and vtables are `linkonce_odr hidden`, so each
unit that uses one has a copy that it can inline, and the linker keeps one
copy. String and location constants stay `private`. The `@export`
functions and the `extern "C"` declarations (spec chapter 11) keep the
linkage they have today.

**Stable text.** A unit's text depends only on what the unit holds:

- its constants and temporaries are numbered within the unit;
- the names of instances, glue and types are derived from what they are
  (the mangled path and the type arguments), not from a global counter.

An edit to one module then changes the text of that module's unit, and of
the units that use something whose layout or signature changed, and no
others.

**One module when asked for.** `--emit=llvm` still writes one module with
the text it has today. `compare-stages` and `verify` compare that text,
and stage 0 does not change. A flag, `--codegen-units=1`, builds that
single module instead of the units, for comparing generated code and for
the release builds that want every function inlinable everywhere.

**Parallel clang.** std gets `os.spawn(program, args) Result<Child,
IoError>`, which starts a process without waiting for it, and
`Child.wait() Result<Output, IoError>`, which waits for it and returns
what `os.run` returns. `os.run` becomes `spawn` followed by `wait`. The
child's output goes to temporary files rather than pipes, so that a child
that writes a lot cannot block. The driver keeps at most **`-j N`**
(`--jobs`) clang processes running. The default is the number of cores
(`os.cpuCount()`, also new). The final link is one more clang call that
gets the objects, the runtime and the `@link` libraries.

**The object cache.** The object of a unit is cached in
`$OFIR_HOME/cache/objects/<hh>/<hash>.o`, where `<hash>` is the SHA-256
of everything clang's output depends on. The runtime hashes the two
input parts without copying the generated LLVM text:

- the unit's text;
- the flags given to clang (profile, `-g`, target);
- the path of clang and the output of `clang --version`.

The cache is shared by every project, so the units of std are compiled
once for each set of instances.

- **Writing.** An object is written to a temporary name and renamed into
  place, so a build that is killed does not leave half an object in the
  cache.
- **Checking.** On a hit, the file must be non-empty and start with the
  magic number of an object file (ELF, Mach-O or COFF). Otherwise it is
  deleted and the unit is compiled.
- **Recovering.** If the link fails while some objects came from the
  cache, those objects are deleted, their units are compiled again
  without the cache, and the link is retried once. Only the second
  failure is reported (E0902).
- **Bounding.** After a build, if the cache holds more than 2 GiB
  (`OFIR_CACHE_LIMIT`, in bytes), the objects used least recently are
  deleted until it holds half that. A hit updates the object's time.
- **Disabling.** `--no-cache` compiles every unit and neither reads nor
  writes the cache.

**Not in this decision.** The front end still checks and lowers the whole
program on each build. Caching it, and checking modules on several
threads, wait until the compiler can use closures, and until the cost of
the front end is measured again after the back end work. The stage 1
`--time-passes` report measures the front end, lowering, code generation,
cache hashing and writing, object compilation, and link.

## Consequences

- A debug build of an unchanged program compiles no unit. A build after
  an edit compiles the units whose text changed, on every core. The
  link, the front end and the generation of every unit's text remain.
- Each unit's text is still generated in full, so a build that hits the
  cache everywhere still costs the front end plus hashing.
- Copies of instances in several units cost compile time on cold builds:
  `List<int>.push` is compiled once for each unit that uses it. The
  copies are what lets a release build inline them, across units.
- Release builds with several units can lose inlining of non-generic
  functions across modules. The release benchmarks (ADR 0018) are run
  with units and with `--codegen-units=1`. If units lose more than 3%,
  release builds default to one unit and keep only the parallel link
  and the cache.
- `-j` and the cache do not change the program that is built. The units,
  their text and therefore the objects are the same whatever the number
  of jobs. The parallel build is tested by building every `run` test
  with units and comparing the output, and with `--codegen-units=1` in a
  CI job.
- Cache keys include the clang version, so upgrading LLVM or switching
  between the LLVM versions tested in CI (ADR 0001) cannot mix their
  objects.
- The design leaves room for the front end cache: units would be
  regenerated only for modules whose inputs changed, with the same object
  cache behind them.
