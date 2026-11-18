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
| `ofir doc [-o dir] [--stdout] [--private] [paths]` | generate HTML documentation (below) |
| `ofir metadata <file.of>` | print the modules of a program as JSON (below) |
| `ofir sbom [--format=cyclonedx\|spdx]` | print the project's bill of materials (below) |
| `ofir audit [--offline] [--message-format=json]` | check the locked packages against advisories (below) |
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
- A help line that a tool can apply has a `fix`: `{"line", "column",
  "endLine", "endColumn", "text"}`, the bytes to replace (counted as the
  span is) and their replacement. The did-you-mean help lines have one; a
  test with a `.fixed` file next to it checks that applying every fix gives
  that file and that it compiles.
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
  "license": "MIT OR Apache-2.0",   // an SPDX license expression (ADR 0081)
  "audit": {"ignore": ["OFIR-2026-0001"]},   // advisories accepted by `ofir audit`
  "trust": {"git": ["https://github.com/acme/"]},   // the accepted sources (ADR 0081)
  "toolchain": ">=0.5.0, <0.7.0",   // the compiler versions it builds with (ADR 0082)
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
requirement, resolution fails (E0912). The packages form a graph without
cycles: a dependency that leads back to a package that depends on it is
E0916 (ADR 0076). Resolution is deterministic: the same manifests, lock and
sources give the same `ofir.lock`, byte for byte, whatever the order of the
keys of `dependencies` (`tools/check_resolution.py`).

Scopes and overrides (ADR 0077, `tools/check_sources.py`):

- `devDependencies`, in the root's manifest and written like
  `dependencies`, are in the graph only under `ofir test` and `ofir
  bench`; a dependency's own are never read. Every command resolves and
  locks them, so the lock does not depend on the command. A key in both
  lists is E0917.
- A dependency object with `"os": "linux"`, `"macos"` or `"windows"` is
  in the graph only when the program is built for that OS; it is locked
  for every OS. Another value is E0910.
- The root's `"overrides": {"text": {"path": "patched/text"}}` replaces
  every dependency whose key is `text`, in every manifest, by that
  directory, relative to the root. An overridden package is not fetched
  or locked; the `overrides` of a dependency are not read.
- `ofir vendor [dir]` copies each locked git and registry package, without
  `.git`, into `dir/<name>` (default `vendor`) and prints the `overrides`
  that use those copies. With them, `--offline` builds with an empty
  `$OFIR_HOME`.
- A manifest with `"workspace": {"members": ["app", "tool"]}` is a
  workspace root. A build in a member uses the root's `ofir.lock` and
  `overrides` (its own are not read); the lock holds the packages of
  every member, and `ofir fetch` at the root resolves them together.

| Command | Effect |
|---------|--------|
| `ofir fetch` | resolves the graph, downloads what is missing, writes `ofir.lock` |
| `ofir update [name]` | resolves again, all packages or one |
| `ofir update --index-reset` | accepts an index whose history was rewritten (E0922) |
| `ofir vendor [dir]` | copies the locked packages into `dir` and prints their `overrides` |
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

Publishing writes the index (ADR 0079). `ofir package` writes
`build/package/<name>-<version>.tar.gz`, the package's files without
`.git` and `build`, and prints their checksum. `ofir publish [--index DIR]
[--dry-run]` packages the project and, in `DIR`, a clone of the index
(default `$OFIR_REGISTRY` when it is a path), appends its line, copies
the archive where a `file://` `download` URL points and commits; the user
pushes the commit, so the git host authenticates. A version is published
once; a bad name or version, a `path` or `git` dependency, or a version
already published is E0919. `ofir yank <version> [--undo]` sets `yanked`
in that line and commits. Names are flat, and who may publish one is the
index host's policy; a mirror of the index or of the archives is checked
by the checksums.

A manifest's `license` is an SPDX license expression (ADR 0081):
identifiers of the SPDX license list, written as the list writes them,
combined with `AND`, `OR`, `WITH` an exception and parentheses, a `+`
after a license, or `LicenseRef-<id>`; anything else is E0910. The list
is embedded in the compiler (`compiler/driver/spdx_list.of`, written by
`tools/gen_spdx.py`). A package without a `license` cannot be published
(E0919), and its index line records it as `"license"`.

`ofir sbom [--format=cyclonedx|spdx]` prints the project's software bill
of materials: CycloneDX 1.5 JSON by default, SPDX 2.3 JSON with
`--format=spdx`. It reads the lock as `--locked` does (E0913 when it is
missing or out of date) and does not count `devDependencies`. The root
is the subject; each package has its name, its version (a git package
without one: its commit), its license, its source and a package URL
(`pkg:ofir/<name>@<version>`, with `repository_url` outside the official
index; `pkg:generic/<name>@<commit>?vcs_url=` for git). The lock's
checksum is the property `ofir:checksum` (in SPDX, the package's
`comment`): it is a hash of the files, not of an archive. `path`
packages and overrides are marked `ofir:source` `local`. The graph of
dependencies is included. The same lock gives the same output: the only
date is `SOURCE_DATE_EPOCH`'s (SPDX needs one: 1970-01-01 without it), and
the SPDX namespace is derived from the lock's SHA-256
(`tools/check_sbom.py`).

`ofir audit` checks each registry package of the lock against the
advisories of its index (ADR 0081), fetching the index unless
`--offline`. The index's `advisories/<package file>` (the package's path
under `advisories/`) has a JSON line per advisory: `id`, `versions`,
`severity` (`low`, `medium`, `high` or `critical`), `summary`, `url`,
`aliases` and `withdrawn`. `versions` holds requirements as in
dependencies, or `<`, `<=` and `>` with a full version, separated by `,`
when all must hold (`">=1.0.0, <1.4.2"`), and alternatives separated by
`||`. An advisory that affects a locked version is E0920, with the lowest
published version that fixes it, and the exit status is 1; one named in
the root's `audit.ignore` is a note instead. A yanked version in the
lock is a warning; git packages have no advisories and are noted.
`--message-format=json` prints the errors and warnings as JSON
diagnostics. `build`, `fetch` and `update` never check advisories, so
a later advisory does not break a build (`tools/check_audit.py`).

The root's `trust` lists the sources it accepts (ADR 0081): the indexes
in `trust.registries` and the git URL prefixes in `trust.git`. With it,
a registry or git dependency anywhere in the graph from a source it does
not list is E0921, before anything is fetched; path dependencies and
overrides are always accepted. Without `trust`, every source is.
`ofir.lock` also has `indexes`, the commit of each registry index read,
sorted by URL; a lock without registry packages has none. `fetch` and
builds keep that commit while the locked versions fit; `update` records
the index's head. When an index is fetched again, a head that does not
descend from the locked commit means its history was rewritten: E0922,
until `ofir update --index-reset` accepts it. With `--locked`, the
releases are read at the locked commit (`tools/check_trust.py`).
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

`make check-fmt-corpus` (part of `make test`) holds the formatter to three
properties over every `.of` file in std, the compiler, the tests and the
examples (`tools/check_fmt_corpus.py`): formatting is idempotent, every `//`
comment survives in order, and the output parses to the same syntax tree as
the input, up to the import order.

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
and `extern` blocks are left out (`--private` documents the private
declarations too); a public declaration without a comment is listed all the
same.

The site works offline (ADR 0075): it refers to no network address. Each
page heads its navigation with the package's name and version, read from
the `ofir.json` of the current directory, and has a search box served by
`search.js`, which lists every documented declaration with its module and
the first sentence of its comment. Documentation for each version goes to
its own directory (`-o build/doc/1.2.0`).

A `` `code` `` span that names a documented declaration links to it: a
name or `Type.member` of the same module, `module.name` of another one, or
a name that exactly one other module declares. Code blocks and signatures
are not linked. The examples of the comments are the doc tests that
`ofir test` runs.

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

Hover (`textDocument/hover`) answers with the declaration of the name at the
position, as Markdown: its first line without the body (the whole line of a
local), in an `ofir` code block, then the text of the `/** ... */` comment
above it. The position may also be on the name of a declaration that is
used in the document.

References (`textDocument/references`) checks every open document, each as
the root of its program, and gives the places where the name at the
position is used for the same declaration, with the declaration's own name
when `context.includeDeclaration` is true: sorted by file and offset,
without duplicates. Rename (`textDocument/rename`) answers with a
`WorkspaceEdit` that replaces each of those places with the new name. It
refuses, with -32602, a new name that is not a name or is a keyword, and,
with -32803, a name it does not link and a declaration outside the open
documents, such as one in std. Uses in files that are not open are not
found, so an editor opens the files to rename across.

Unsaved edits reach every phase: a document of a dependency (a `path` or git
package) that is open is read in place of its file, so the documents that
use it are checked against the unsaved text once they change
(`tests/lsp/packages`).

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
document, `$DIR` its directory and `$STD` the standard library's directory.

### Watch mode, scratch files and the REPL

A scratch file needs no manifest: `ofir run notes.of` builds and runs one
file. `--watch` on `build`, `run`, `test` or `check` runs the command again
each time an `.of` file or an `ofir.json` under the input's directory
changes (ADR 0074):

```
ofir test --watch src/parser_test.of
```

`ofir repl` reads entries from standard input. An entry ends at the end of
a line where its brackets are balanced. An item (`fn`, `struct`, `import`
and so on) goes at the top of the session, a `let` or `const` binding into
its `main`, and any other entry is an expression whose value is printed
(unless it is `()`), or else a statement:

```
> let xs = [1, 2, 3]
> xs.push(4)
> xs
[1, 2, 3, 4]
```

Each entry builds and runs the whole session again, and the REPL shows
only the new output. A binding keeps its value because its initializer
runs again; side effects outside the program happen again too. An entry
that does not compile or panics is dropped. `:show` prints the session
module, `:reset` empties it and `:quit` ends the REPL.

## 3. Build profiles

`--profile=NAME` chooses a profile (ADR 0078); `--release` is
`--profile=release`, and the two cannot both be given. `ofir bench`
builds with `release` unless `--profile` says otherwise.

| Profile | clang flags | `@cfg(debug)` | LLVM modules |
|---------|-------------|---------------|--------------|
| `dev` (default) | `-O0` | true | one per Ofir module |
| `release` | `-O2` | false | one |

The root manifest's `profiles` (the workspace root's in a workspace)
changes these and names others. `inherits` is `"dev"` (the default) or
`"release"`, `opt` is the optimization level (0 to 3), `debugInfo`
acts like `-g` and `trimPaths` like `--trim-paths`:

```jsonc
"profiles": {
  "release": {"debugInfo": true},
  "fast": {"inherits": "release", "opt": 3}
}
```

A profile that does not exist, an unknown key or a bad value is E0918.
Safety checks are never disabled by a profile. The optimizer removes the
checks it can prove unnecessary. `tools/check_build_profiles.py`, run by
`make test`, checks the profiles and the native inputs below.

### Native inputs

A package's `"build": {"native": ["c/fast.c"]}` lists C files, relative
to its manifest. The C files of every package of the program are passed
to the clang call that links it, so they are compiled with the profile's
flags and `--target`; the program declares their functions in an
`extern "C"` block. A missing file is E0918, and a static library
(`--lib=static`) cannot have native files yet. No code of a package runs
at build time: there are no build scripts, and generated code is either
computed by `comptime` or committed.

A build at `-O0` also passes `-mllvm -pre-RA-sched=linearize` to the
clang calls that generate code: it changes nothing observable and makes
clang about 40% faster on large functions (ADR 0054). A profile with a
higher `opt` does not, since the AArch64 backend crashes with it there. There is no other
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

The flag is opt-in, and it is stage 1's: without it the `.ll` is the
one stage 0 writes. A profile with `"debugInfo": true` turns it on.

The debug info names the real paths of the sources and the working
directory, so a debugger finds them. `--trim-paths`, or
`"trimPaths": true` in a profile, names the logical paths of a panic
location instead (spec 6.5), with the directory `.` (ADR 0083).

### Reproducible builds

The same inputs give the same bytes (ADR 0083). The inputs are the root
file and what it imports, the packages pinned by `ofir.lock`, the
standard library, `ofir.json` and the profile, the compiler's version,
the clang version, the target, the flags, the `OFIR_*` variables and
`SOURCE_DATE_EPOCH`; the directory, the user, `HOME`, the time and the
locale do not change the output. The `.ll`, the executable or library,
`ofir doc`'s site, `ofir sbom`'s documents and `make dist`'s archive are
covered; with `-g`, only with `--trim-paths`.

`--record=FILE` writes the inputs and the SHA-256 of each artifact as
JSON: the versions, the target, the profile and the flags, the hash of
the runtime and of `ofir.lock`, the logical path and hash of each source
file, and the name and hash of each artifact. The variables that name
paths (`OFIR_STD`, `OFIR_RUNTIME`, `OFIR_CLANG`) are recorded through
the hashes of what they point to. Two records of the same inputs are the
same bytes, so comparing them shows which input differs.
`tools/check_reproducible.py`, run by `make test`, builds a project twice
in different directories, homes, times and umasks and compares the
artifacts.

### Debugging in an editor

An editor debugs an Ofir program through a Debug Adapter Protocol
adapter that reads DWARF, `gdb -i=dap` (gdb 14 or later) or `lldb-dap`;
Ofir has none of its own (ADR 0072). Build with `-g`, then point the
editor's DAP client at the adapter and the executable. In VS Code, with
an extension that runs `lldb-dap`:

```json
{
  "type": "lldb-dap",
  "request": "launch",
  "name": "Debug main",
  "program": "${workspaceFolder}/build/main",
  "preLaunchTask": "ofir build -g"
}
```

and `"request": "attach"` with `"pid"` to attach to a running program.
Launch, attach, line and function breakpoints, stepping, the stack and
the variables work; `tools/check_dap.py`, run by `make test-stage1`,
checks them with each adapter it finds.

### Coverage and profiling

`--coverage` (stage 1) counts how many times each source line runs, with
gcov counters that clang adds and maps to the lines of `-g` (ADR 0073).
It implies `-g` and turns the object cache off. Each run adds to the
`.gcda` files next to the build's `.gcno` files:

```
ofir build --coverage main.of -o build/main
build/main
llvm-cov gcov build/*.gcda        # main.of.gcov: count or ##### per line
gcovr --gcov-executable "llvm-cov gcov" --html-details -o coverage.html
```

`gcovr` and `lcov` export HTML, Cobertura XML or LCOV. The lines of a
generic function count every instance; code inlined by `--release`
counts at the line it was written on. clang needs its profile runtime
(`libclang-rt-18-dev` on Ubuntu).

To profile, build with `-g` (or `--release -g`) and run the program
under any profiler that reads DWARF, such as `perf record`, `valgrind
--tool=callgrind` with `callgrind_annotate`, or Instruments. A function
appears as `ofir.<name>`, a generic instance with its type arguments
(`ofir.twice<int>`), and `main` as `ofir_main`. `tools/check_profile.py`, run by `make
test-stage1`, checks both and prints their measured overhead.

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
├── share/ofir/
│   ├── errors/              explanations for `ofir explain`
│   ├── LICENSE
│   ├── version              the active version (written by install.sh)
│   └── files                the files of the active copy
├── install.sh
└── toolchains/<version>/    every installed version, in this layout
```

The compiler finds these files relative to its executable, so an
installed compiler needs no environment variable and no repository
(ADR 0082). A release is one archive per target,
`ofir-<version>-<target>.tar.gz`, built by `make dist` with its
`.sha256`; it holds `ofir-<version>/` in this layout, without
`toolchains/`. Its `install.sh` installs it:

```
sh install.sh [--prefix DIR] [--sums FILE] [--no-verify] [ARCHIVE]
sh install.sh [--prefix DIR] --use VERSION     switch the active version
sh install.sh [--prefix DIR] --list
sh install.sh [--prefix DIR] --remove VERSION
sh install.sh [--prefix DIR] --uninstall
```

The prefix defaults to `$OFIR_HOME`, then `~/.ofir`. The checksum is
checked before anything is written. Every version stays in
`toolchains/`, so `--use` rolls back; `--uninstall` removes only the
files the installer wrote. A manifest's `toolchain` that the running
compiler is not in is E0923, whose help names the installed version to
`--use`. `ofir version` prints the version and the prefix.

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
