# 0082. Installation and toolchain management

- Status: Proposed
- Date: 2026-11-18

## Context

OFIR-69 asks for archives that install on a clean machine, pinned
toolchains, checksums, rollback and uninstall. Today the only way to get
the compiler is to build it from the repository (`make stage1`) or from a
release's seed (`docs/bootstrap.md`). An installed compiler already
finds its files relative to its executable, in the layout of
`docs/design/toolchain.md` section 5: the standard library in
`<prefix>/lib/ofir/std`, the runtime in `<prefix>/lib/ofir/libofir_rt.a`
and the error explanations in `<prefix>/share/ofir/errors`. But nothing
produces that layout, nothing installs it, a package cannot say which
compiler version it needs, and there is no way back to the previous
version.

rustup keeps every toolchain side by side and reads a
`rust-toolchain.toml`; Go 1.21 reads the `toolchain` line of `go.mod`
and downloads the version it names; Zig and Node ship plain archives and
leave version switching to other tools. A compiler that downloads
compilers needs a trusted release host, which the project does not run
yet.

## Decision

### Archives

1. **A release is an archive per tier 1 target**,
   `ofir-<version>-<target>.tar.gz`, where `<target>` is the LLVM triple
   of ADR 0065 (`x86_64-linux-gnu`, `aarch64-apple-darwin`,
   `x86_64-w64-windows-gnu`, ...). It holds one directory,
   `ofir-<version>/`, in the installation layout: `bin/ofir`
   (`bin/ofir.exe` on Windows), `lib/ofir/std`, `lib/ofir/libofir_rt.a`,
   `share/ofir/errors`, `share/ofir/LICENSE`, and the installer,
   `install.sh`. The tree is relocatable: nothing in it names the
   directory it is extracted to.
2. **`make dist`** builds the archive for the host from stage 1 at `-O2`,
   with the files sorted, owned by 0:0, and their times set to
   `SOURCE_DATE_EPOCH` (the last commit's time when unset), so that the
   same commit gives the same bytes on the same host (OFIR-70 extends
   this). Next to it, `ofir-<version>-<target>.tar.gz.sha256` holds the
   SHA-256 checksum in the format of `sha256sum`.
3. **The release workflow attaches the archives** and a `SHA256SUMS`
   file listing all of them, next to the seed. Each archive is built and
   tested on its own target's runner before the release is created.

### Installation

4. **`install.sh`** is a POSIX shell script (Git Bash on Windows, which
   the toolchain already needs, section 6 of the toolchain design). It
   needs `tar`, `sha256sum` or `shasum`, and nothing else; clang stays a
   separate requirement, as today.

   ```
   sh install.sh [--prefix DIR] [ARCHIVE]   install a version and use it
   sh install.sh --prefix DIR --use VERSION  use an installed version
   sh install.sh --prefix DIR --list         the installed versions
   sh install.sh --prefix DIR --remove VERSION
   sh install.sh --prefix DIR --uninstall    remove everything it installed
   ```

   The prefix defaults to `$OFIR_HOME`, then `~/.ofir`. Run from an
   extracted archive with no `ARCHIVE`, it installs that archive's tree.
5. **Each version is kept.** Installing copies the tree to
   `<prefix>/toolchains/<version>/`, then makes it the active one: the
   active version is a copy of its tree at the prefix itself
   (`<prefix>/bin/ofir`, `<prefix>/lib/ofir`, `<prefix>/share/ofir`), so
   the compiler finds its files as it does today, on every system,
   without symbolic links. `<prefix>/share/ofir/version` names the
   active version, and `<prefix>/share/ofir/files` lists the files of
   the active copy.
6. **An archive is checked before anything is written.** Its checksum
   must match the `.sha256` next to it, or the `SHA256SUMS` named by
   `--sums FILE`; a missing or wrong checksum stops the installation
   with nothing changed, unless `--no-verify` is given. The installer
   extracts into a temporary directory under the prefix and moves the
   result into place, so an interrupted installation leaves the
   previous active version working.
7. **Rollback is `--use`.** It replaces the active copy with another
   installed version; the version that was active stays in
   `toolchains/`. `--remove` deletes a version that is not active.
8. **Uninstall removes only what the installer wrote**: the files listed
   in `share/ofir/files`, `toolchains/`, and the directories left empty.
   `<prefix>/cache` and anything else the user put in the prefix stay.
   The installer does not edit shell profiles; it prints the line that
   puts `<prefix>/bin` on `PATH`.
9. **Clean-machine contract.** An installed compiler needs no
   environment variable and no repository: `ofir build`, `run`, `test`,
   `fmt`, `doc` and `explain` work from the prefix alone with clang on
   `PATH` (or `OFIR_CLANG`). `OFIR_STD` and `OFIR_RUNTIME` stay for
   contributors.

### Pinned toolchains

10. **A manifest can name the toolchain it needs.** The root's
    `ofir.json` takes `"toolchain": "<range>"`, a range of versions in the
    syntax of advisories (ADR 0081): requirements of dependencies, or
    `<`, `<=` and `>` with a full version, joined by `,` and `||`
    (`"0.6.0"`, which is `^0.6.0`, or `">=0.6.0, <0.8.0"`). A compiler
    whose version is not in it stops before resolving with E0923, naming
    both, and the help line names the command that switches to an
    installed version in the range (`sh <prefix>/install.sh --use
    0.6.2`) when one is installed. An invalid range is E0910. A
    dependency's `toolchain` is not read; in a workspace, the root's
    applies.
11. **The compiler does not download compilers.** Fetching a toolchain
    needs a trusted release host and signed releases; until the project
    runs one, a pinned version is installed from its archive, by the
    user or by CI. `ofir version` prints the version and the prefix it
    runs from.

### Errors

12. New code: E0923 (the running compiler does not satisfy the
    manifest's `toolchain`). An invalid `toolchain` is E0910.

## Consequences

- A user installs a release with two commands and no build, and goes
  back to the previous version with one.
- Several versions take disk space side by side: a version is a few
  megabytes, mostly the standard library's sources.
- `install.sh` is a second program to keep working on three systems;
  `tools/check_install.py` checks it on each CI runner: it builds the
  archive, installs it into an empty prefix with `OFIR_STD` and
  `OFIR_RUNTIME` unset and the repository out of reach, builds and runs a
  program, installs a second version, rolls back, rejects an archive
  with a wrong checksum, and uninstalls to an empty prefix.
- A project pins its compiler, and an older or newer compiler says so
  instead of failing on a language difference.
- Downloading toolchains, signing releases and system packages (Homebrew,
  apt, winget) are later decisions; each starts from these archives.
