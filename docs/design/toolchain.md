# Toolchain and build process

## 1. The `ofir` command

A single executable provides every tool:

| Command | Description |
|---------|-------------|
| `ofir new <name>` | create a project with `ofir.json` and `src/main.of` |
| `ofir build [--release]` | compile the project to an executable |
| `ofir run [--release] [-- args]` | build and run |
| `ofir check` | run every phase up to the borrow checker, no code generation |
| `ofir test [filter]` | build and run `@test` functions and doc tests |
| `ofir bench [filter]` | build with optimizations and run `@bench` functions |
| `ofir fmt [--check] [--stdout] [paths]` | format source files (below) |
| `ofir doc [-o dir] [--stdout] [paths]` | generate HTML documentation (below) |
| `ofir metadata <file.of>` | print the modules of a program as JSON (below) |
| `ofir explain <code>` | describe an error code |
| `ofir lsp` | run the language server on standard input and output (below) |
| `ofir version` | print version and LLVM/clang versions in use |

A single file can be compiled without a project: `ofir run hello.of`.

### Debug flags (for compiler developers)

| Flag | Effect |
|------|--------|
| `--emit=tokens` | print tokens and stop |
| `--emit=ast` | print the AST and stop |
| `--emit=types` | print the types of the root module's declarations and locals, and stop |
| `--emit=names` | print what the root module's names resolve to (locals, declarations, imports) and stop |
| `--emit=air` | print AIR after drop elaboration |
| `--emit=llvm` | write the `.ll` file next to the output |
| `--emit=asm` | write assembly |
| `--time-passes` | print the time spent in each phase |
| `--message-format=short` | one line per diagnostic: `file:line:col: error[E0300]: message` |
| `--message-format=json` | diagnostics as JSON objects, one per line (below) |

### Notes and help

A diagnostic may end with lines that are not about its span: a `note`
gives context, a `help` suggests a fix. The human format writes them
after the excerpt, each on its own line:

```
error[E0304]: cannot assign twice to immutable variable `n`
 --> src/main.of:3:5
  |
3 |     n = 2
  |     ^
  |
  = help: declare it with `let n` to assign to it again
```

The short format leaves them out: it has one line per diagnostic.

### Suggestions

An unknown name (E0200), field (E0316) or method (E0301) gets a help
line naming the closest one that exists, when one is close enough:

```
error[E0200]: cannot find `cuont` in this scope
 --> src/main.of:3:13
  |
3 |     println(cuont)
  |             ^^^^^
  |
  = help: did you mean `count`?
```

- The candidates are the names visible at that point (locals, the
  module's declarations, its imports and the prelude) for a name, the
  type's declared names for a type, the struct's fields for a field and
  the methods the type declares for a method (std's for a primitive).
- Closeness is the edit distance between the bytes of the two names:
  the insertions, deletions, substitutions and swaps of two adjacent
  bytes that turn one into the other. A candidate is close enough
  when the distance is at most a third of the unknown name's length,
  rounded down, so names shorter than three bytes get no suggestion.
- The closest candidate wins; between equally close ones, the name
  interned first. Both compiler stages suggest the same name.

### JSON diagnostics

With `--message-format=json` every diagnostic is written to standard error
as one JSON object per line, sorted as the human format sorts them:

```json
{"severity":"error","code":"E0300","message":"mismatched types: expected int, found str","file":"src/main.of","line":3,"column":13,"endLine":3,"endColumn":18,"notes":[]}
```

- `severity` is `"error"` or `"warning"`; `code` is the registry code
  (`docs/errors`).
- `line` and `column` start at 1 and count bytes; `endLine` and
  `endColumn` are the position just after the span.
- `notes` holds the notes and help lines in order, each an object with
  `kind` (`"note"` or `"help"`) and `message`; it is `[]` without them.
- Driver errors that point at no source, as E0900 (cannot read a file) or
  E0902 (clang failed), have no `file`, `line`, `column`, `endLine` or
  `endColumn`.
- Strings are escaped as JSON requires: `\"`, `\\`, `\n`, `\t` and
  `\u00XX` for other control characters; other bytes are copied.

The format is stage 1's: stage 0 accepts only `short` and `human`.

### `ofir explain`

`ofir explain E0600` prints the explanation of a code: the file
`E0600.md` of `docs/errors`, with what the rule is, an example that
triggers it and how to fix it. The file is looked up in, in order:

1. the directory named by the `OFIR_ERRORS` environment variable;
2. `<prefix>/share/ofir/errors`, next to an installed compiler;
3. the repository's `docs/errors`, for a compiler in `build/stageN`.

The exit status is 0 when the explanation is printed, 1 when no file
has it and 2 when the argument is not a code (`E` or `W` and four
digits). The command is stage 1's.

### Compiler service

Editors and build tools drive the compiler through its commands; this is
the contract they rely on (ADR 0053). Changing it is a breaking change.

`ofir check <file.of>` runs every phase up to the move checker, as
`ofir build --stop-after=check`: it writes the diagnostics and no output.
The options of `build` apply, `--message-format` among them.

`ofir metadata <file.of>` loads the program, the root and every module it
imports, and prints one JSON object on standard output, on one line:

```json
{"format":1,"compiler":"0.5.0","root":"main","modules":[
  {"name":"main","path":"src/main.of","std":false},
  {"name":"std.core","path":"/usr/lib/ofir/std/core/index.of","std":true}]}
```

`format` is the version of this shape: a field may be added without
raising it, never dropped or renamed. The modules come in load order, the
root first; `path` is the file as the compiler read it, and `std` tells
the standard library's modules. Names and types are not checked: an
import that names no module is reported by `ofir check`, and a file that
does not lex or parse gives its diagnostics and status 1, with nothing on
standard output.

Diagnostics always go to standard error, in the format `--message-format`
asks for; with `json`, every line is one diagnostic (above), so a tool
reads standard output for results and standard error for diagnostics.

The exit status of every command:

| Status | Meaning |
|--------|---------|
| 0 | success: no errors (warnings allowed, unless `--deny-warnings`) |
| 1 | errors were reported, or a test or bench failed |
| 2 | usage error: unknown command or option, missing argument |
| 101 | internal compiler error: the compiler panicked |
| other | `ofir run`: the program's own status (101 if it panicked) |

A build may be cancelled at any point by a signal (SIGINT, SIGTERM).
Nothing it leaves behind breaks the next build: an object enters the
cache only by a rename once clang has written it whole, and the outputs
are written again by the next build. `tools/check_service.py` checks the
statuses, `metadata` and builds interrupted at several points.

## 2. Project layout

```
my_app/
├── ofir.json
├── src/
│   ├── main.of             root module of an executable
│   └── net/
│       └── http.of         module net.http
└── build/                  generated, ignored by git
    ├── debug/
    └── release/
```

### `ofir.json`

The manifest is JSON with comments (`//`, `/* */`) and trailing commas
allowed, the format of `tsconfig.json` (ADR 0026). Keys are camelCase.

```jsonc
{
  "name": "my_app",
  "version": "0.1.0",
  "edition": "2027",
  "build": {
    "kind": "exe",            // "exe" or "lib"
    "root": "src/main.of",
  },
  "dependencies": {          // packages (ADR 0032)
    "util": {"path": "../util"},
  },
  "fmt": {                    // what `ofir fmt` produces (docs/style-guide.md)
    "inlineBlocks": false,    // a function or statement body never shares a line with its braces
  },
}
```

`fmt` properties:

| Property | Default | Meaning |
|----------|---------|---------|
| `inlineBlocks` | `false` | when `false`, the body of every function and of every statement (`if`, `else`, `for`, `unsafe`, `match` arm, `const ... else`) is broken onto its own lines; an `if` or `unsafe` used as a value may stay on one line if each block holds one expression. `true` lets a body of a single statement stay on one line. |

`ofir fmt` (stage 1) does not read the manifest yet: it always applies the
default. `tools/check_format.py` checks the repository against the same
rules, and `--fix` applies them.

### Dependencies

The package manager (ADR 0032, roadmap 6.5, stage 1) reads the
`dependencies` of `ofir.json`. Each key is a package name, the first
segment of its imports (`import json.parser`); each value says where it
comes from:

| Value | Source |
|-------|--------|
| `"^1.2"` | the registry (6.5e) |
| `{"git": URL, "version": "^0.3"}` | a git repository; versions are its tags `vX.Y.Z` |
| `{"git": URL, "rev": "abc123"}` / `"branch": "dev"` | one commit, or the head of a branch |
| `{"path": "../util"}` | a directory, relative to `ofir.json` |

A requirement is `^1.2.3` (the default), `~1.2.3`, `=1.2.3`, `>=1.2.3` or
`*`. A program has one version of each package; when no version fits every
requirement, resolution fails (E0912).

| Command | Effect |
|---------|--------|
| `ofir fetch` | resolves the graph, downloads what is missing, writes `ofir.lock` |
| `ofir update [name]` | resolves again, all packages or one |
| `--locked` | `build`, `run`, `test` and `fetch` fail instead of changing `ofir.lock` (E0913) |
| `--offline` | uses only the cache |

`ofir.lock` sits next to `ofir.json` and is generated: each package's
version, source and SHA-256 checksum, sorted by name. Commit it for
programs. Downloads are cached under `$OFIR_HOME` (default `~/.ofir`).

Implemented so far (6.5c, stage 1): `path` and `git` dependencies, read
transitively; a package is the same when its canonical directory is. A
`git` value that is not a URL (`://`, or `git@`) is a local path,
relative to `ofir.json`. A repository is cloned bare into
`$OFIR_HOME/cache/git/<hash of its URL>` and each version used checked
out into `$OFIR_HOME/cache/src/<name>-<version>-<hash>`, by running `git`.
`build`, `run` and `test` resolve too: they keep the locked versions that
still fit, fetch a repository only when it is missing or has no fitting
version, and write `ofir.lock` when it changed; `ofir update` fetches
again and chooses the highest versions. Each git package's checksum is
the SHA-256 of its files, sorted by path, each as its path, a NUL, its
size, a NUL and its bytes, `.git` left out; a checkout that does not
match the lock is E0914 (6.5d). With `--offline`, a repository missing
from the cache is E0911 and nothing is fetched.

A registry dependency is a version requirement, `"json": "^1.2"` or
`{"version": "^1.2"}` (6.5e). The registry's index is a git repository,
`$OFIR_REGISTRY` (default `https://github.com/ofir-lang/index`), cloned
bare like a git dependency. Its file for a package (`1/a`, `2/ab`,
`3/a/abc`, `js/on/json`) has a JSON line per published version:
`{"version", "dependencies", "checksum", "yanked"}`. Its `config.json` has
`{"download": URL}`, where `{name}` and `{version}` stand for the
package's; the archive, a `.tar.gz` of the package's files, is downloaded
with `curl` into `$OFIR_HOME/cache/archives` and extracted with `tar`
into `$OFIR_HOME/cache/src/<name>-<version>-<hash>`. The highest version
that fits and is not yanked is chosen; a locked one is kept while it
fits, yanked or not. Its files must have the index's checksum (E0914);
the lock records the index and the version, with no commit. A package
missing from the index is E0911, and the manifest of the archive, not
the index's `dependencies`, gives its dependencies.
The modules of a package are found from the directory of its
`build.root` (default `src/index.of`), which is the module named like the
package; a package imports its own modules by their names in it, as a
project does.

### `ofir fmt`

`ofir fmt` formats the `.of` files of the paths it is given (files, or
directories read recursively without their hidden entries; by default the
current directory) in place, in the style of [the style guide](../style-guide.md).
`--check` writes nothing: it prints the files that would change and exits
with status 1 if there are any. `--stdout` prints the formatted text instead
of writing it.

The formatter works on the tokens and comments of a file, not on its syntax
tree. It keeps the line breaks of the source and every comment, and changes
only:

- the indentation, computed from the open brackets: 4 spaces per block and
  4 more for a line that continues a statement, unless the continuation is
  aligned deeper (with an opening `(`, say), which is kept;
- the spaces inside a line (none inside brackets or around `.`, one after
  `,` and `:` and around binary operators);
- blank lines: at most one in a row, none at the start or end of a block;
- inline blocks, broken onto their own lines (`inlineBlocks` above);
- a `,` added at the end of a multi-line list in `( )` or `[ ]`;
- the order of each run of `import` lines, sorted.

A file that does not lex or parse is reported and left as it is, and before
a file is written the formatter checks that its tokens are unchanged but
for the added commas.

### `ofir doc`

`ofir doc` (stage 1) writes HTML documentation from the documentation
comments (spec 12.5, ADR 0020) of the `.of` files of the paths it is given:
files, or directories read recursively without their hidden entries and
without the `_test.of` files. By default it reads `src` when that is a
directory, the current directory otherwise. The code is in `compiler/doc`.

Each file is a module, named by its path from the directory given (`/`
becomes `.`, and `index.of` names its directory): `net/http.of` is
`net.http`. A file given by itself is named after its stem, or its directory for
an `index.of`. The pages go to
`build/doc` (`-o dir` chooses another directory): one `<module>.html` per
module and an `index.html` that lists the modules with the first paragraph
of each module comment. `--stdout` writes nothing: it prints the page of
each module instead.

A page holds the module comment, then the public declarations in the order
of the file, each with an anchor named after it (`#parse`, `#Point.x`): its
signature as written, without its attributes and body, on one line, and its
comment. A struct lists its public fields and methods, an enum its variants
and public methods, an interface its methods. Private declarations, imports
and `extern` blocks are left out; a public declaration without a comment is
listed all the same.

A comment's text is Markdown, of which `ofir doc` renders paragraphs
(separated by blank lines), `-` lists, fenced code blocks and `` `code` ``
spans; anything else is text. A line that starts with a tag starts a section,
which runs up to the next line that starts with a tag:
`@typeParam` and `@param` become lists of names and descriptions,
`@returns`, `@panics`, `@safety`, `@example` and `@see` titled paragraphs,
and `@deprecated` a note before the text.

A file that does not lex or parse is reported and left out; the exit status
is then 1.

### `ofir lsp`

`ofir lsp` (stage 1) is a language server: it speaks the Language Server
Protocol 3.17, JSON-RPC messages with a `Content-Length` header, on standard
input and output, and exits when the client sends `exit` (status 0 after
`shutdown`, 1 otherwise). The code is in `compiler/lsp`.

It keeps the text of each open document, synchronized whole
(`textDocumentSync.change` is 1), and checks a document each time it is
opened or changed, as `ofir build --stop-after=check` would, with every open
document read in place of its file. The diagnostics are published for that
document: positions count lines from 0 and columns in UTF-16 code units,
errors have severity 1 and warnings 2, the code is the diagnostic's code and
the notes and help follow the message on their own lines. A diagnostic
located in another file, such as an imported module, is shown at the start
of the document, its message preceded by `path:line:column:`. Closing a
document clears its diagnostics.

Go to definition (`textDocument/definition`) checks the document again and
answers with the declaration of the name at the position: a local or a
parameter, a declaration of the document or of an imported module, the
module of an import (its file, at the start), or a type written in a type.
The range is the whole declaration. Fields, methods and the declarations of
the prelude are not linked yet; the answer is then `null`.

Completion (`textDocument/completion`, asked for again after `.`) inserts a
probe, a name no program declares, at the position, and checks the document
with it. Where the resolver or the type checker reports the probe unknown,
the names visible there are kept, as for a "did you mean" suggestion
(`compiler/sema/complete.of`):

- after `module.`, the module's public declarations;
- after `value.`, the fields of its struct and the methods that take
  `self`;
- in a type, the types and modules in scope;
- elsewhere, the locals in scope, the declarations of the module, those it
  imports and the prelude's, and the keywords.

The answer is a `CompletionList` of the names that start with the part of a
name before the position, sorted. The rest of the document must parse, and
after `value.` it must also resolve: otherwise only the keywords are
offered.

Other requests are answered with the error -32601 (method not found), and a
message that is not JSON with -32700.

Requests are handled one at a time, in the order they arrive, and every
request gets its answer before the next is read. A notification the server
does not know, `$/cancelRequest` among them, is ignored: a request is never
cancelled, it is answered. Diagnostics are published after each `didOpen`
and `didChange`, before the next message is read, so a client sees them in
the order of its edits.

`tests/lsp` holds the tests: a `.of` file, an optional `.lsp` file with the
client's messages, and the server's messages in `.out`, where `$URI` is the
document and `$STD` the standard library's directory.

## 3. Build profiles

| Profile | clang flags | Checks | Debug info |
|---------|-------------|--------|------------|
| `debug` (default) | `-O0 -g` | all | yes |
| `release` | `-O2` | all (checked arithmetic and bounds stay on) | no |
| `release-fast` **[planned]** | `-O3 -march=native` + LTO | all | no |

Safety checks are never disabled by a profile. The optimizer removes the
checks it can prove unnecessary.

A debug build also passes `-mllvm -pre-RA-sched=linearize` to the clang
calls that generate code: at `-O0` it changes nothing observable and makes
clang about 40% faster on large functions (ADR 0054). There is no other
backend and no JIT.

### Debug info

`-g` makes the compiler emit DWARF (functions, line tables, locals and
their types) and passes `-g` to clang, so a debugger (`lldb`, `gdb`) can
set breakpoints on functions and source lines, step through them and
print parameters and locals:

- each user function, closure included, gets a `DISubprogram` at the
  line of its declaration;
- each instruction gets the line and column of the statement it was
  lowered from, the `return` or the tail expression included;
- compiler-generated functions (drop and clone glue, thunks, vtables)
  have no debug info, so stepping skips them.
- the prologue that stores the parameters has no line, so a breakpoint
  on a function stops after it, at its first statement;
- each named parameter and local gets a `DILocalVariable`, declared
  with `llvm.dbg.declare` on its stack slot (through the pointer for a
  local held by reference or captured by a closure); a generic instance
  has the types of its instance;
- a type is a basic type (`bool`, the integers, the floats), a pointer
  to its pointee (a thin reference or pointer), a struct with its
  fields at the offsets of their slots, `str` with its bytes, length and
  capacity, or, for any other type (an enum, a tuple, an array, a
  function, a fat pointer), a struct of its size with no fields.

With `--release -g` the optimizer keeps what it can: a breakpoint on a
line still stops, and a local it removed prints as `<optimized out>`;
the compile unit says the code is optimized. `tools/check_debugger.py`,
run by `make test` when gdb is installed, checks both builds.

Until projects exist the flag is opt-in, and it is stage 1's: without
it the `.ll` is the one stage 0 writes. The `debug` profile will turn
it on.

## 4. Build steps

```
1. read ofir.json, locate the root module
2. parse the root module and, transitively, every imported module
3. check (resolve, type check, borrow check) all modules
4. lower, monomorphize and generate build/<profile>/<name>.ll
5. clang <flags> <name>.ll libofir_rt.a -o build/<profile>/<name>
```

(ADR 0033, roadmap 6.6): step 4 writes one unit per source module, and
step 5 becomes two steps. First, at most `-j N` clang
processes (`--jobs`, by default the number of cores) compile the units
to objects. Then one more clang call links the objects with the runtime.
An object whose unit's text, clang flags and clang version are unchanged
is reused from `$OFIR_HOME/cache/objects`:

| Flag | Effect |
|------|--------|
| `-j N`, `--jobs N` | at most `N` clang processes at a time |
| `--codegen-units=1` | one module for the whole program, as `--emit=llvm` writes |
| `--no-cache` | neither read nor write the object cache |

A `--release` build is always one unit, compiled to one object that the
cache keeps like any other: with a unit per module, LLVM cannot inline a
function of one module into another, and the self-hosted compiler built
that way ran 59% slower (roadmap 6.6f).

The cache holds at most `OFIR_CACHE_LIMIT` bytes (2 GiB by default):
once a build passes the limit, the objects used least recently are
deleted until it holds half. A cached object that is not an ELF, Mach-O
or COFF object is deleted and compiled again; if the link fails with
cached objects, their units are compiled again and the link is retried
once.

The compiler finds:

- the **standard library** in `$OFIR_HOME/std` (default: next to the
  executable, `../lib/ofir/std`);
- the **runtime** in `$OFIR_HOME/lib/libofir_rt.a`;
- **clang** through `$OFIR_CLANG`, otherwise `clang` on the `PATH`.
- In stage 0, which has no projects yet: the runtime through
  `$OFIR_RUNTIME`, otherwise `libofir_rt.a` in the parent directory of the
  compiler (`build/libofir_rt.a` for `build/stage0/ofir`). `ofir build
  file.of` writes `./file` (or `-o`); `ofir run file.of` builds into a
  temporary directory and runs the program.
  Supported: LLVM 17 to the latest release, each tested in CI (ADR 0001).
  `ofir version` reports the clang in use.

## 5. Installation layout

```
<prefix>/
├── bin/ofir
├── lib/ofir/
│   ├── std/                 standard library sources
│   └── libofir_rt.a        runtime
└── share/ofir/errors/       explanations for `ofir explain`
```

## 6. Targets

Version 1 supports 64-bit Linux (x86_64, aarch64), macOS (aarch64,
x86_64) and Windows (x86_64). On Windows the compilers pass
`--target=x86_64-w64-windows-gnu` to clang: the runtime and the programs
use the MinGW C library, so a MinGW-w64 toolchain (for example WinLibs)
must be installed next to clang. Cross-compilation uses
`--target=<llvm triple>` and relies on clang's cross-compilation support.

## 7. Repository build (for contributors)

```
make stage0        # build bootstrap/ with the system C compiler → build/stage0/ofir
make runtime       # build runtime/ → build/libofir_rt.a
make stage1        # compile compiler/ with stage0 -O2 → build/stage1/ofir
make stage2        # compile compiler/ with stage1 → build/stage2/ofir
make verify        # stage2 compiles compiler/ again; outputs must be identical
make test          # run all suites in tests/ against the newest stage
```

`make stage1` builds the compiler in release mode so repeated compilations
run faster. It reuses the executable until a compiler or std source, stage 0,
the runtime library, or the build configuration changes. This does not set
`--release` for programs compiled by stage 1.

Requirements: a C11 compiler, `make`, clang ≥ 17, and Python 3 for the
test runner (a development dependency only; users never need Python).
On Windows, run `make` from a POSIX shell (Git Bash); `tools/platform.mk`
builds with clang for the MinGW target and finds clang on `PATH` or in
`C:/Program Files/LLVM/bin`.

### Windows

Ofir runs on 64-bit Windows with the MinGW C library (section 6). To
install it:

1. Install [Git for Windows](https://gitforwindows.org/), which brings
   Git Bash, and Python 3 for the tests.
2. Install LLVM 17 or later (the official installer puts clang in
   `C:/Program Files/LLVM/bin`, which the build finds without `PATH`).
3. Install a MinGW-w64 toolchain, such as WinLibs, for example in
   `C:/mingw64`. It provides the C library, `mingw32-make` and the
   winpthreads library the runtime links with. Put its `bin` directory on
   the `PATH` of Git Bash: `export PATH="/c/mingw64/bin:$PATH"`.
4. In Git Bash, clone the repository and build the compiler:

```
mingw32-make stage1                    # build/stage1/ofir.exe
build/stage1/ofir.exe run examples/hello.of
```

The compiler finds the runtime (`build/libofir_rt.a`) and the standard
library (`std/`) relative to its executable, as on the other platforms;
`OFIR_RUNTIME`, `OFIR_STD` and `OFIR_CLANG` override them. `ofir build`
writes `.exe` files. `mingw32-make test` runs the tests as CI does; the
ones that use POSIX paths or tools carry `// requires: unix` and are
skipped.

A release seed (`docs/bootstrap.md`) works on Windows as on the other
platforms: `mingw32-make check-seed SEED=ofir-<version>.ll` builds a
compiler from it.
