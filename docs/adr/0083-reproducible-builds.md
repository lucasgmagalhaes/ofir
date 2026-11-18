# 0083. Reproducible builds

- Status: Proposed
- Date: 2026-11-18

## Context

OFIR-70 asks for builds that give the same bytes from the same inputs,
with the inputs and the artifacts declared, and for evidence from
independent builds that differ in paths and times.

Most of it already holds: without `-g`, building the same project from
two directories gives the same executable, `ofir sbom` dates its output
only with `SOURCE_DATE_EPOCH` (ADR 0081), and `make dist` sorts and dates
its archive (ADR 0082). The differences that are left:

- a panic location embeds the path of its file as the compiler was given
  it: `at /home/user/app/src/main.of:12:17` when the file was named
  absolute, and always the absolute path of a standard library file
  (`compiler/codegen/llvm.of`, `locationGlobal`), while the spec's
  example (section 6.5) shows `at src/main.of:12:17`;
- the `.ll` names its root file in `source_filename`, as given;
- with `-g`, each `DIFile` names the working directory
  (`compiler/codegen/debug.of`);
- a PE executable carries the link time unless the linker is told
  otherwise;
- the runtime archive is built with `ar rcs`, which on macOS stores the
  times and owners of its members.

Cargo writes absolute paths unless `--remap-path-prefix` or the newer
`trim-paths` is given; Go records module paths (`-trimpath` made the
default for module builds); Zig writes paths relative to the package
root. reproducible-builds.org asks for `SOURCE_DATE_EPOCH` and no build
path in the output.

## Decision

### Inputs and artifacts

1. **The inputs of a build are declared**: the root file and the files it
   imports, the packages pinned by `ofir.lock` (their checksums), the
   standard library of the running compiler, `ofir.json` and the
   selected profile, the compiler's version, the clang version, the
   target, the command-line flags, and these variables: `OFIR_*`,
   `SOURCE_DATE_EPOCH`. Nothing else (the directory, the user, `HOME`,
   the time, the locale, the order of a directory listing) changes the
   output.
2. **The artifacts covered** are the `.ll`, the executable or library
   that `ofir build` writes, `ofir doc`'s site, `ofir sbom`'s documents,
   and the archive of `make dist`. Two builds with the same inputs give
   the same bytes for each.

### Paths

3. **A panic location names a logical path** in both stages: relative to
   the project root (the directory of `ofir.json`, or else the root
   file's directory), `std/<path>` for a standard library file, and
   `<name>-<version>/<path>` for a file of a package. `source_filename`
   uses the same path. A file outside all of them keeps the path it was
   given.
4. **Debug info keeps real paths by default**, so a debugger finds the
   sources without configuration. `--trim-paths`, or `"trimPaths": true`
   in a profile, writes the logical paths of decision 3 with the
   directory `.` instead; `release` does not turn it on.

### Times and identities

5. **No timestamps.** The Windows link gets `-Wl,--no-insert-timestamp`;
   an ELF build-id and a Mach-O UUID are hashes of the content, which
   clang already makes them. The runtime archive is built with
   `ar rcsD` (and `ZERO_AR_DATE=1` on macOS), so it is the same on every
   build.

### Record and evidence

6. **`ofir build --record=FILE`** writes a JSON document of the inputs of
   decision 1 (versions, target, profile, flags, variables, the
   checksums of the root's files and of the lock) and the SHA-256 of each
   artifact it wrote. Two records of the same inputs are the same bytes;
   comparing two of them shows which input differs.
7. **`tools/check_reproducible.py`**, run by `make test`, builds the same
   project (with a dependency, a panic and `-g --trim-paths`) twice, in
   two different directories with different `HOME`, `OFIR_HOME`, times
   and umasks, and compares the `.ll`, the executable, the record, the
   docs, the SBOM and the dist archive byte for byte.

### Docs and errors

8. The spec (section 6.5) states the logical path of a panic location;
   the toolchain design lists the inputs, `--trim-paths` and
   `--record`. No new error code: a bad `trimPaths` value is E0918,
   like the other profile keys.

## Consequences

- A downstream builder can check a release by building it again and
  comparing hashes, and a CI cache can key on the record.
- A panic message no longer shows where the file was on the machine
  that built it; the logical path is what the user reads in their
  project anyway.
- Debug builds stay convenient by default; a reproducible debug build is
  one flag away.
- Byte-identical builds hold for one host and one clang version: a
  different clang or a different target is a different input, and
  cross-host identity is not promised.
