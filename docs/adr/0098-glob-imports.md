# 0098. Glob imports

- Status: Proposed
- Date: 2026-11-20

## Context

A module that uses many declarations of another imports each one by
name, or imports the module and qualifies every use. A file of
`compiler/` often starts with ten or more `import a.b.X` lines from the
same module, and adding a use means adding an import.

## Decision

1. **`import path.*`** imports every public member of the module `path`:
   its public declarations and its re-exports (spec 7.3). Private
   declarations, the module's own imports and its submodules are left
   out, as they are for `path.x`.
2. **Globs bind last.** After the declarations, the prelude, the
   re-exports and the imports by name of every module are bound, each
   glob fills only the names its module leaves free or gives to the
   prelude. A module's own declaration or an import by name always wins,
   so adding a declaration to an imported module cannot break a module
   that already has that name.
3. **Ambiguity is reported on use.** When two globs bring the same name
   for different declarations, the name binds to neither; using it is
   E0207, which names both globs. A name that is not used is not an
   error, so a library adding a declaration breaks only the modules that
   use the new name through two globs, and an import by name fixes them.
4. **No `as`, no `pub`.** A glob takes no alias; `pub import path.*` is
   E0208, so a module's public members stay the ones it names.
5. **Unused globs warn.** A glob counts as used when the module uses a
   name it binds; otherwise it is W0002, like any import.
6. **Stage 1 only.** Stage 0 is frozen (docs/bootstrap.md); globs are a
   stage 1 extension, tested by `// requires: stage1` tests, and
   `compiler/` does not use them until a release ships them.

## Consequences

- Reading a module no longer tells where every name comes from; an
  editor's go-to-definition and `ofir check --emit=names` still do.
- A glob over a large module makes its public names visible in the whole
  importing module, below its own names, so a typo can resolve to a
  declaration of the glob's module instead of being E0200.
- `pub import path.*` can be added later without breaking programs, if
  re-exporting whole modules is needed.
