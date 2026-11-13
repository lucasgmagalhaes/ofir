# 7. Modules

## 7.1 Files and modules

- Every `.of` file is a module. The module name is the file name.
- A directory groups modules into a package: `src/parser/lexer.of` is the
  module `parser.lexer`. A file named `index.of` inside a directory is the
  module named after the directory itself (`src/parser/index.of` is `parser`).
- A file `name_test.of` next to `name.of` holds that module's tests
  (chapter 12.2): it is part of the module, not a module of its own.
- A program is built from a root file containing `fn main()`.

## 7.2 Imports

```ofir
import std.io
import std.collections.Map
import parser.lexer
import parser.ast as ast
```

- Imported modules are accessed by their last name segment:
  `lexer.tokenize(src)`, or by the alias given with `as`.
- Importing a type (`import std.collections.Map`) brings that name into scope.
- Imports are resolved relative to the project root and to the standard
  library. The project root is the nearest directory above the root file
  that holds an `ofir.json`, or else the root file's own directory; a root
  file inside a project is named after its path from there
  (`compiler/base/intern.of` is `base.intern`).
- Circular imports are allowed; declarations in a module can reference each
  other in any order.
- Importing the same declaration or module twice into a module is allowed
  (a test file may repeat the imports of its module).
- An unused import produces a warning (chapter 12.6).
- A path can go through several modules: `geo.shapes.area()` or the type
  `geo.shapes.Square`, when `geo` re-exports `geo.shapes` (7.3). Every
  segment but the last must name a module that the previous one makes
  public.
- A submodule is not a member of its parent: `import parser` does not make
  `parser.lexer` reachable. Using it without an import or a re-export is
  E0200, with a help that names both fixes.

### Initialization

A module runs no code when a program starts. A module-level `const` or
`let` initializer is evaluated at compile time (chapter 15): it may use
literals, constants, operators and `comptime` calls, and anything else is
E0801. So the order in which modules are loaded is not observable, and
import cycles cannot see a value before it is initialized. A constant that
depends on itself, directly or through other modules, is E0800.

## 7.3 Visibility

Declarations are private to their module by default. `pub` makes them
visible to importers:

```ofir
pub struct Token {
    pub kind: TokenKind
    pub text: str
    line: int          // private field
}

pub fn tokenize(src: str) List<Token> { ... }
```

### Re-exports

`pub import` makes an imported module or declaration a public member of the
importing module:

```ofir
// geo/index.of
pub import geo.shapes              // `geo.shapes.area()` works for importers of `geo`
pub import geo.shapes.Circle       // `geo.Circle`
pub import geo.shapes.Kind as Shape
```

- The re-exported name is the last segment, or the alias. It also binds in
  the module itself, like a plain `import`, and never counts as unused.
- Only a public declaration can be re-exported (E0202 otherwise).
- A plain `import` is private: modules do not re-export their imports
  unless they say `pub import`.
- Re-exports may chain through several modules. A cycle of re-exports that
  never reaches a declaration names nothing (E0200).

## 7.4 The prelude

The following names are available in every module without imports:

- primitive types, `str`, `List`, `Map`, `Option`, `Result`, `Box`, `any`
- the built-in functions below

They are the public declarations of `std.core` and of its submodules
(`std.core.box`, ...), which every program loads.

## 7.5 Built-in functions

```ofir
fn print(text: str)              // standard output
fn println(text: str)            // standard output, followed by "\n"
fn eprint(text: str)             // standard error
fn eprintln(text: str)           // standard error, followed by "\n"
fn panic(message: str) !
fn sizeOf<T>() int
fn alignOf<T>() int
```

Values are printed through string interpolation: `println("{count}")`.
There are no variadic functions in Ofir.

`assert`, `assertEq` and `assertNe` are not built in: they are in
`std.testing` and are imported like any other std name (12.2, ADR 0036).

## 7.6 Standard library (bootstrap subset)

| Module | Contents |
|--------|----------|
| `std.io` | stdin/stdout/stderr, buffered readers and writers |
| `std.fs` | read and write files, directories |
| `std.os` | command line arguments, environment, exit, run processes |
| `std.strings` | string helpers: split, trim, find, parse numbers (named so it does not shadow the `str` type) |
| `std.math` **[planned]** | constants and functions such as `math.PI`, `math.hypot`; float methods like `x.sqrt()` live on the type |
| `std.mem` | `swap`, `replace`, `take` |
| `std.mem.cell` | `UnsafeCell`, `Cell` |
| `std.testing` | `assert`, `assertEq`, `assertNe`, `Bencher` (12.2, 12.3) |

These are the modules the self-hosted compiler needs. The rest of the
standard library is **[planned]**.
