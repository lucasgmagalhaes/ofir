# 0080. Tools and rules for code written by agents

- Status: Proposed
- Date: 2026-11-18

## Context

Most Ofir code is now written and changed by AI agents, and most of
an agent's time goes to three costs: the tokens it reads to understand
the code around a change, the edit, check and fix rounds it needs, and
the wrong guesses that cause those rounds. A language that serves
agents well keeps all three low.

Ofir already does well on some of this. Diagnostics come as JSON with
codes, notes and machine-applicable fixes (5.12b, 5.12c, 6.28c);
`ofir check` and `ofir metadata` are a stable service interface
(ADR 0053); `ofir explain` describes every code. There is no
overloading (ADR 0016), imports name what they bring in (spec 7.2),
there are no implicit numeric conversions (spec 2.1), return types are
written (spec 3.3), and `ofir fmt` gives one layout.

Three gaps remain:

1. **Navigation goes through the editor.** An agent works from a shell
   and does not speak LSP. To find a signature, the callers of a
   function or the types that satisfy an implicit interface (ADR 0004)
   it reads whole files or greps, which costs many tokens and still
   misses: a grep does not know which `parse` is meant, and nothing
   names the implementers of a structural interface.
2. **A signature does not say enough.** To know whether a call does
   I/O, allocates or is unsafe, or what a function expects from its
   arguments, an agent reads the body, and the bodies of what it
   calls.
3. **Some mistakes compile.** A program that compiles but fails far
   from the cause costs the most rounds. Stage 0 evaluates
   `list[i] = f()` with the place taken before `f()` runs, so when
   `f` grows `list` the store goes to freed memory; the spec does not
   say which side is evaluated first.

This record leaves out two other ideas, to be decided on their own:
teaching agents the language (diagnostics for syntax from other
languages, a compact reference printed by the compiler) and a suite
that measures agents at fixed tasks.

## Decision

### Query and edit commands

1. **The language server's answers are commands too.** Each command
   loads the program like `ofir check`, answers on standard output and
   follows ADR 0053: diagnostics on standard error, the documented exit
   statuses, `--message-format=json` for one versioned JSON object
   (fields are added without raising `format`, never dropped or
   renamed). A position is written `file:line:column`, 1-based, columns
   in UTF-8 bytes. A symbol is written as `ofir doc` links it:
   `module.name` or `module.Type.member`.

   | Command | Answer |
   |---------|--------|
   | `ofir outline <paths>` | each declaration of the files, nested by type, with its signature on one line, its attributes and its line range; no bodies |
   | `ofir sig <symbol>` | the signature, documentation comment, contracts and effects (below) of one declaration, and where it is |
   | `ofir type <position>` | the type of the expression or binding at the position, and the declaration it names, if any |
   | `ofir def <position>` | where the name at the position is declared |
   | `ofir refs <symbol or position>` | every use in the program, with the line of each |
   | `ofir impls <interface>` | the types that satisfy the interface, with where each is declared; `ofir impls --of <type>` lists the interfaces of the program a type satisfies |
   | `ofir callers <function>` | the functions that call it directly |

   `--private` is not needed: private declarations are always listed,
   since an agent edits them too. A text answer is one line per item,
   so `head` and `grep` work on it.

2. **Edits are commands.** They write the files they change, print
   each file they wrote, and change nothing when the program has errors
   or the edit would introduce one (exit status 1, with the
   diagnostics). `--dry-run` prints a unified diff instead.

   | Command | Edit |
   |---------|------|
   | `ofir fix [paths]` | applies every machine-applicable fix the diagnostics offer, repeated until none is left or a round applies nothing |
   | `ofir rename <symbol> <name>` | renames a declaration and every use, across the whole program |
   | `ofir move <symbol> <module>` | moves a top-level declaration to another module and updates the imports of every module that used it |

3. **The commands and `ofir lsp` share one implementation**, the one in
   `compiler/lsp`. A query answers the same in both.

### Diagnostics

4. **Cascades are suppressed.** An expression whose type is an error
   has the error type, and no diagnostic is reported about a value of
   the error type or a name whose declaration failed. A program with
   one mistake reports one diagnostic. `--error-limit=N` (default 50)
   stops after N errors and says how many more were found.

### Signatures that say enough

5. **Contracts.** A function may carry `@requires(cond)` and
   `@ensures(cond)` attributes, any number of each. `cond` is a `bool`
   expression over the parameters; in `@ensures`, `result` names the
   returned value. A contract cannot call a function with effects
   (below) or mutate.

   ```ofir
   @requires(low <= high)
   @ensures(result >= low && result <= high)
   fn clamp(value: int, low: int, high: int) int {
       if value < low {
           return low
       }
       if value > high {
           return high
       }
       return value
   }
   ```

   Contracts are checked in the debug and test profiles and not in
   release; a profile setting turns them on in release. A failed
   `@requires` panics at the call, naming the caller's span and the
   condition; a failed `@ensures` panics in the callee. `ofir sig`,
   `ofir outline` and `ofir doc` show them, so a contract is the
   documentation of what a function expects and promises.

6. **Effects.** The compiler computes, for each function, which of
   three effects it may have:

   - `io`: calls an `extern` function, or the operating-system layer
     of `std`;
   - `alloc`: allocates on the heap;
   - `unsafe`: contains an `unsafe` block.

   An effect comes from the body or from a callee. A call through a
   function value or an interface value has every effect. A generic
   function's effects are those of each instantiation. Panics are not
   an effect: with checked arithmetic and bounds checks nearly every
   function may panic, and `@panics` in the documentation names the
   deliberate ones.

   `ofir sig` and `ofir outline` show the computed effects. A function
   may declare an upper bound, `@effects()`, `@effects(alloc)`,
   `@effects(io, alloc)`, and a body that exceeds it is an error that
   names the call bringing in the effect. Public functions of `std`
   declare their bound, so a change that adds an effect to one cannot
   pass unnoticed.

### Rules for the language

7. **Locality is a rule for every language change.** A function must
   stay understandable from its body and the signatures of what it
   names. A proposal that would weaken any of these says so in its
   ADR, which needs to show why: no overloading, explicit imports
   without wildcards, written return types on functions, no implicit
   conversions, no hidden control flow (no exceptions, no implicit
   calls beyond destructors and the interfaces of spec 9).

8. **One way to write a thing.** A proposal that adds a second syntax
   for something the language already expresses needs to show why.
   `ofir fmt` stays without options that change the meaning of the
   layout, and `ofir check --fmt` reports the files `ofir fmt` would
   change, as warnings, so an agent formats before it commits.

9. **A mistake that compiles is a defect of the language.** When a
   program compiles and then fails in a way that a rule of the language
   could have reported, the fix is that rule or a defined behavior,
   not a line in a list of pitfalls. The first case: in an assignment
   the right side is evaluated before the place on the left, so
   `list[i] = f()` is correct whatever `f` does to `list`.

## Consequences

- An agent reads `ofir outline` of a file before the file, and
  `ofir sig` of a call before the callee's body. A large file such as
  `compiler/codegen/llvm.of` is read by range, after its outline.
- Renames and moves no longer need search and replace, and do not
  leave a program that fails to build.
- `ofir impls` answers the question structural interfaces left open;
  ADR 0004 stands.
- The query commands load the whole program for each answer, as
  `ofir check` does. If they prove slow on large programs, a
  long-lived service with a cache in memory comes then (ADR 0053).
- Contracts and effects are new attributes: spec 12.1 lists them and
  each new error gets a code in `docs/errors`. Contract checks cost
  time in debug builds; release builds are unchanged unless turned on.
- Effects are computed over the call graph, a new pass in both stages.
  Calls through function values and interfaces have every effect, so a
  function that takes a closure cannot declare a narrow bound; a
  bound on the closure's type is left for later.
- Suppressing cascades changes the expected output of compile-fail
  tests that relied on the extra diagnostics.
- Defining the order of evaluation in assignments changes stage 0 and
  stage 1 together, so their output still matches.
- The remaining stage 0 pitfalls in `CLAUDE.md` are reviewed under
  rule 9; those that are behavior, not missing diagnostics, stay.
