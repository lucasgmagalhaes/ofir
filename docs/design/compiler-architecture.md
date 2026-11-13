# Compiler architecture

This document describes how the Ofir compiler is organized internally. It
applies to both the stage 0 (C) and stage 1 (Ofir) compilers; stage 0
implements a subset of each phase.

## 1. Pipeline

```
                ┌──────────┐   ┌────────┐   ┌──────────┐   ┌────────────┐
    .of files ─►│  lexer   │──►│ parser │──►│ resolver │──►│type checker│
                └──────────┘   └────────┘   └──────────┘   └─────┬──────┘
                  tokens          AST        symbols, scopes      │ typed AST
                                                                  ▼
 ┌────────┐   ┌──────────┐   ┌─────────┐   ┌──────────┐   ┌────────────┐
 │ clang  │◄──│ codegen  │◄──│  mono   │◄──│  drops   │◄──│  lowering  │
 └───┬────┘   └──────────┘   └─────────┘   └────┬─────┘   └─────┬──────┘
     │          LLVM IR       concrete AIR       │ AIR + drops    │ AIR
     ▼                                      ┌────┴─────┐          │
 executable                                 │  borrow  │◄─────────┘
                                            │  checker │
                                            └──────────┘
```

| Phase | Input | Output | Errors reported |
|-------|-------|--------|-----------------|
| Lexer | bytes | tokens | invalid characters, unterminated literals |
| Parser | tokens | AST | syntax errors |
| Resolver | AST | symbol table, resolved names | unknown names, duplicate definitions, import cycles in constants |
| Type checker | resolved AST | types for every node | type mismatches, missing methods, unsatisfied bounds, non-exhaustive matches |
| Lowering | typed AST | AIR (generic) | — |
| Borrow checker | AIR | — | use after move, conflicting borrows, escaping references |
| Drop elaboration | AIR | AIR with explicit drops | — |
| Monomorphization | generic AIR | concrete AIR | recursion limit |
| Codegen | concrete AIR | LLVM IR text | — |
| clang | LLVM IR + runtime | executable | (internal errors only) |

Phases after type checking never report user errors except the borrow
checker and the instantiation depth limit of monomorphization. If any phase reports an error, compilation stops after that
phase (the phase itself continues to collect as many errors as possible).

## 2. Core data structures

### Identifiers instead of pointers

All compiler entities are stored in arenas and referenced by typed integer
indices. This makes data structures compact, cache friendly, trivially
serializable, and deterministic.

| Index | Refers to |
|-------|-----------|
| `FileId` | a source file in the file table |
| `NodeId` | an AST node |
| `DefId` | a declaration (function, struct, enum, interface, const, ...) |
| `TypeId` | an interned type |
| `LocalId` | a local variable inside one AIR body |
| `BlockId` | a basic block inside one AIR body |
| `Symbol` | an interned identifier string |

### Spans

Every token, AST node and AIR statement carries a `Span { file, start, end }`
with byte offsets. Line and column are computed only when rendering a
diagnostic.

### Type interning

Types are hash-consed: each distinct type exists exactly once and is
identified by its `TypeId`. Type equality is integer comparison.

```
TypeKind = Int(width, signed) | Float(width) | Bool | Unit | Never | Str
         | Adt(DefId, args: [TypeId])        // struct, enum
         | Ref(mutable, TypeId) | Ptr(mutable, TypeId)
         | Slice(TypeId) | Array(TypeId, len) | Tuple([TypeId])
         | Fn([TypeId], TypeId) | Dyn([DefId]) | Param(index)
         | Infer(let) | Error
```

`Error` is compatible with every type; it is produced after a reported
error so that one mistake does not cause a cascade of follow-up errors.

### Memory

The compiler allocates almost everything from arenas that live for the whole
compilation and are freed at once at exit. The compiler itself therefore
needs no fine-grained memory management, which keeps stage 0 simple and fast.

## 3. Lexer

- Hand written, single pass over bytes, no backtracking.
- Emits a newline token at the end of each line unless the innermost open
  bracket is `(` or `[` (it keeps a stack of open brackets). It does not
  decide whether the newline ends a statement; the parser does.
- Emits `>` as a single token; the parser joins adjacent `>` tokens (no
  space between them) into `>>`, `>=` and `>>=` in expression contexts.
  Likewise, a float token such as `0.1` after a `.` in field position
  (`pair.0.1`) is split by the parser into two tuple field accesses.
- String interpolation is split into tokens:
  `"a {x} b"` → `StrStart("a ")`, tokens of `x`, `StrMid(" b")`... `StrEnd`.
  The lexer keeps a small stack of brace depths to know when `}` closes an
  interpolation.
- Doc comments are kept as tokens attached to the next item; ordinary
  comments are dropped (the formatter uses a separate trivia-preserving mode).

## 4. Parser

- Recursive descent for items and statements, Pratt (precedence climbing)
  for expressions, matching the precedence table of spec chapter 4.8.
- Newlines (spec 1.3): the parser consumes a newline as a terminator only at
  points where the current statement may end. Every grammar position that
  requires more tokens (after a binary operator, `,`, `=`, `=>`, an opening
  bracket, ...) calls `skip_newlines()` first.
- Struct-literal restriction (`if x == P{...}`) is handled with a
  "no struct literal" flag, as in Go.
- Generic disambiguation (`f<T>(x)` vs comparisons) uses bounded
  speculative parsing as specified in spec 8.5.
- **Error recovery:** on an unexpected token, the parser reports one error
  and skips to the next statement terminator or closing brace at the same
  nesting level, then continues. Each file yields as many independent
  syntax errors as possible.
- The AST is a faithful representation of the source; no desugaring happens
  in the parser.

## 5. Resolver

Two passes over each module:

1. **Collect:** register every top-level declaration in the module scope and
   record imports. Load imported modules (each file is parsed once).
2. **Resolve:** walk function bodies, binding each identifier to a `DefId`,
   a local, a type parameter or a module.

Scopes form a chain: block → function → module → prelude. Methods are
collected per type in a `MethodTable` keyed by `(DefId of type, Symbol)`.

Types and values share one namespace, as in Go: `struct Point` and
`fn Point` in one module conflict (E0201). A module declaration may hide a
prelude name. The parameters and the top level of a function body are one
scope, so `fn f(x: int) { const x = 1 }` is an error, while a nested block may
shadow `x`.

Stage 0 indexes its tables by symbol id: the module table maps each symbol
to a `DefId`, and locals use a binding stack (each declaration saves the
binding it hides, leaving a block restores them), so a lookup is one array
access.

## 6. Type checker

### Strategy

**Bidirectional type checking with local inference**: each expression is
checked against an *expected type* when one is known, otherwise its type is
*synthesized*. Inference variables are used only inside a function body, for:

- integer and float literals (defaulting to `int` / `float`);
- type arguments of generic calls (`max(a, b)`);
- closure parameters;
- empty collection literals (`[]`).

Unification is first-order; signatures are always explicit, so inference
never crosses function boundaries. This keeps type checking linear in
practice and error messages local.

**Stage 0** checks the bodies of the standard library lazily: a std
function's signature is checked with its module, but its body only when the
lowering reaches it (`sema_check_late`), followed by its move analysis
(`sema_moves_late`). A program uses a small part of the prelude, so the
cost of `check` does not grow with std. The root module and the modules
outside std are checked whole; `make test-std` checks every std module
whole by taking each as the root.

### Method resolution

For `recv.m(args)`:

1. Look up `m` in the method table of the type of `recv`, auto-dereferencing
   through references (`&&T` → `&T` → `T`).
2. Then in the default methods of interfaces that the type satisfies.
3. Check `recv` against the mode of the method's `self` (borrow, `mut`,
   `move`): for `mut self` it must be a mutable place, for `move self` it is
   moved.

### Interfaces

- Satisfaction `T: I` is computed structurally and cached per `(TypeId, DefId)`.
- For generic code, a bound `T: I` makes `I`'s methods callable on `T`.
- For `dyn I`, the checker records the `(concrete type, interface)` pairs
  that need a vtable; codegen emits one constant vtable per pair.

### Patterns and exhaustiveness

Patterns are type checked against the scrutinee type. Exhaustiveness and
unreachable-arm detection use the *usefulness* algorithm (Maranget,
"Warnings for pattern matching"), which also produces the list of missing
patterns for the error message.

Stage 0 (`sema/exhaustive.c`) splits enums, bool, tuples, structs and `()`
by constructor; integers, floats and `str` have infinitely many values, so
only `_` or a binding covers them (ranges that cover a whole integer type
are not recognized). Arms with a guard never count as covering. The error
shows one uncovered value; an arm that earlier arms already cover gets
W0004.

## 7. AIR — Ofir Intermediate Representation

AIR is a control-flow graph of basic blocks per function, similar in spirit
to Rust's MIR. It is the input to borrow checking, drop elaboration,
monomorphization and code generation.

```
Body       = { locals: [Local], blocks: [Block], params: [LocalId] }
Local      = { type: TypeId, name: Symbol?, mutable: bool, span }
Block      = { statements: [Statement], terminator: Terminator }

Statement  = Assign(Place, Rvalue)
           | StorageLive(LocalId) | StorageDead(LocalId)
           | Drop(Place)                 (inserted by drop elaboration)
           | SetDropFlag(LocalId, bool)

Place      = LocalId + projections: [Field(i) | Deref | Index(LocalId) | Downcast(variant)]

Rvalue     = Use(Operand) | BinaryOp(op, Operand, Operand) | CheckedBinaryOp(...)
           | UnaryOp(op, Operand) | Ref(mutable, Place) | Aggregate(kind, [Operand])
           | Cast(kind, Operand, TypeId) | Discriminant(Place) | Len(Place)

Operand    = Copy(Place) | Move(Place) | Constant(value)

Terminator = Goto(BlockId)
           | Switch(Operand, [(value, BlockId)], otherwise: BlockId)
           | Call(callee, [Operand], destination: Place, next: BlockId)
           | Assert(Operand, expected: bool, PanicKind, next: BlockId)
           | Return | Unreachable
```

Stage 0 implements a subset (`bootstrap/src/air/air.h`): places are whole
locals until structs arrive, and calls are statements rather than
terminators, because Ofir has no unwinding: a call either returns or ends
the process. The driver lowers, validates and generates one function at a
time, so only one AIR body is in memory.

Properties:

- Every expression with side effects is broken down into statements; all
  operands are places or constants.
- Control flow (`if`, `match`, `for`, `?`, `&&`, `||`) becomes explicit
  blocks and switches.
- Runtime checks (overflow, bounds, division by zero) are explicit `Assert`
  terminators, so later passes can remove the ones proven redundant. Both
  stages fold them (`air/fold`): within a block, a temporary assigned from
  constants is known, and an `Assert` whose failure condition is known false
  becomes a `Goto`; the assignments left unread are then removed. Dividing
  or shifting by a constant needs no check; a narrowing cast of a constant
  that does not fit keeps its check.
- Both stages validate every body after lowering and again after check
  folding, in every build (`air/air_validate.c`, `air/validate.of`): the body
  has its return local, parameters and entry block, every block is
  terminated, every jump names a block and every operand a local of the
  body. A malformed body is a compiler bug: the compiler prints the body,
  a note and ``ofir: internal error: invalid AIR after <pass>: AIR of
  `fnN`, bbM: <fault>``, then aborts, before any LLVM IR is written.

### Lowering highlights

| Source construct | AIR |
|------------------|-----|
| `a + b` (integers) | `CheckedBinaryOp` + `Assert(no overflow)` |
| `xs[i]` | `Len` + `Assert(i < len)` + `Index` projection |
| `x?` | `Discriminant` + `Switch`; the error arm assigns the return place and jumps to the return block |
| `match` | decision tree of `Switch` on discriminants and literals |
| `for x in list` | index loop with `Len` and `Index`; the element is copied (Copy types) or borrowed |
| `for x in iter` | loop calling `next()` and switching on `Option` |
| string interpolation | calls to `Display.format` on a fresh `str` buffer |
| closures | an environment struct + a function taking it as first parameter |
| method call | plain call with `recv` as the `self` argument |
| borrowed / `mut` argument | `Ref` (shared or exclusive) of the argument place, or of a temporary holding the value |
| `move` argument | `Move` of the place (`Copy` for Copy types) |

## 8. Borrow checker

Runs on generic AIR, once per function, using dataflow analysis over the CFG:

1. **Initialization and moves** — forward "maybe initialized / maybe moved"
   analysis per place. Reading a place that may be uninitialized or moved is
   an error.
2. **Borrows** — each `Ref` creates a loan, including the implicit ones
   created for borrowed and `mut` arguments. A loan is live from its creation
   to the last use of the reference (liveness analysis). A write, move or
   conflicting borrow of a place overlapping a live loan is an error.
3. **Escape rules** — checks the second-class reference rules: references and
   `ref struct` values may not be stored into non-ref aggregates or returned,
   except through the single-reference-parameter rule (spec 5.6).

Because references cannot escape, every loan is local to one function body:
no inter-procedural analysis and no lifetime inference are needed.

**Stage 0** (`sema/moves*.c`) implements step 1 on the typed AST instead of
AIR, right after a function is type checked, because the AST still says
which uses consume a value and which borrow it, and its control flow is
structured: branches join their states, loops are walked to a fixpoint.
It tracks whole variables and the field paths moved out of them, the
variables that borrow (borrowed parameters, `for` variables, bindings of a
matched borrowed place), and the `mut` parameters that must hold a value
again at each return. Step 3 is in `sema/check_ref.c`; step 2, the borrow
checker proper, is in stage 1 only (roadmap 4.10).

**Stage 1** (`compiler/sema/borrows.of`) does step 2 in the same walk as
the move analysis. The state at each point also keeps the loans that may be
alive: a loan is a borrowed place of a local and its *holder*, a local
whose value holds a reference (`const r = &list`, `const f = list.first()`, a
borrowing `for` or `match` binding) or a `for` loop. The value being
computed carries the loans it holds, and a `const`, `let` or assignment
gives them to the local. A call result that holds a reference borrows the
argument it comes from (spec 5.6). An access to a place overlapping a loan
it is not compatible with records a conflict, which is reported only when
the holder is used afterwards: that gives non-lexical borrows without a
separate liveness pass. The `mut` arguments of a call are borrowed after
all the arguments are evaluated (two-phase borrows), and the places of the
arguments are checked against each other.

Step 3 in stage 1 adds two rules to `sema/check_ref.of`'s checks of
stored and returned types: a borrowed function type `fn(A) R` (a `Fn`
type with `data` `FN_BORROWED`, while `move fn` is `FN_OWNED`) is second-
class like a reference, so it cannot be a field, a variant payload, a
container element or a return type (E0702). And a borrow cannot outlive
its block: at the end of each block, and at a `break` or `continue` for
the blocks it leaves, the owned locals declared in it are dropped (a
`Drop` access, which conflicts with any loan on them); a loan that is
still used afterwards, or carried by the block's value, is E0706.

## 9. Drop elaboration

Using the move analysis results, inserts `Drop(place)` at:

- scope exits (`StorageDead`) of initialized locals with drop glue;
- assignments that overwrite an initialized value;
- unwinding of `?` and early `return`.

Where a value is moved on some paths only, a boolean **drop flag** local is
introduced and the drop becomes conditional. LLVM removes most of these flags
after optimization.

**Stage 0** (`air/lower_drop.c`, `air/lower_scope.c`) elaborates drops
while lowering, with what the move analysis left in the AST: the
expressions that move a local out, the new values that are consumed, the
locals that only borrow, and the field paths moved out somewhere. Every
owned local gets a drop flag (and so does every moved part of it); a new
value that nothing consumes (a call result used as an argument, an
interpolated string) is a temporary, dropped at the end of its full
expression: a statement, a condition, a `match` arm or the right side of
`&&`, `||` and `??`. The last expression of a block or an arm passes its
value on to the enclosing expression instead. Writes through raw pointers
drop nothing (`mem.replace`, `List.push`). Drop glue is an instance like a
generic function, `drop<T>`, for each concrete type that needs it.
`tools/check_leaks.py` runs every program of `tests/run` under valgrind.

**Drop glue** for a type is generated once: call the user `drop` method if
present, then drop each field (and, for enums, the active variant's fields).

**Clone glue** (`air/lower_clone.c`) is its counterpart, `clone<T>(value: T)
T`, for each concrete type that needs drop: it calls T's `clone` method if
present, and otherwise rebuilds the value from clones of its fields (of the
active variant, for an enum). A value of a type without drop is cloned by
copying it. `x.clone()` on a type without a `clone` method and std's
`cloneOf<T>` intrinsic both lower to it; `sema/check_clone.c` decides which
types can be cloned (E0322).

## 10. Monomorphization

Starting from `main` (or all `@test` functions), a worklist collects every
function instance `(DefId, [TypeId])` reachable through calls, vtables and
function values. Each instance gets a concrete copy of the AIR body with
type parameters substituted. Instances are deduplicated by their key.
Drop glue and vtables are instantiated in the same pass.

Symbol names are mangled deterministically:
`_A` + module path + item name + encoded type arguments.

**Stage 0** (`bootstrap/src/air/mono.c`). There are no bounds yet, so a
generic body lowers the same way for every instance: each instance is
lowered from the generic function, with TY_PARAM types, and its type
arguments are then substituted into every type of the AIR body. A call
records its callee's type arguments, in terms of the caller's parameters;
after the substitution they are concrete, and the callee instance is
reached. Instances are keyed by `(DefId, TypeId of the argument tuple)`
in a hash set. Names stay readable for profiling: the instance's
arguments, qualified by module outside the root, follow the item name
(`@"ofir.boxes.wrapped<geo.Point>"`, `%"Pair<int, str>.5"`).

## 11. Code generation

AIR is translated to **textual LLVM IR** (`.ll`), see ADR 0001.

- Each AIR local becomes an `alloca` in the entry block; LLVM's `mem2reg`
  and `SROA` passes promote them to registers.
- Integer overflow checks use `llvm.sadd.with.overflow.*` and friends;
  the panic branch calls a `cold noreturn` runtime function.
- With `--release`, `mut` and `&mut T` parameters are emitted with
  `noalias`; borrowed and `&T` parameters with `noalias readonly` when the
  type has no interior mutability (fat borrows, closures, `cfn`s and glue
  get neither). The AIR marks a `mut` parameter `exclusive`. These
  attributes are only sound because the borrow checker has run: **stage 0,
  which has no borrow checker, never emits them**.
- Internal functions use `fastcc` and `internal` linkage, so LLVM can inline
  and specialize freely; `pub extern "C"` functions use the C ABI.
- An enum is `{ i32 tag, [K x i64] payload }`, each variant a struct laid
  over the payload. An enum with a niche (spec 13.3: two variants, one
  empty and one holding a thin reference, a `Box` or a `fn`) is the
  pointer alone, `{ ptr }`: a place naming its tag reads
  `select (icmp eq ptr p, null), none, some`, storing the empty variant's
  tag stores null, and the other variant's tag store is dropped (its
  payload is already there). Both stages do this, with the same IR.
- A struct's LLVM type holds its fields by descending alignment, keeping
  declaration order among equals (spec 13.2), so `{ u8, int, u8 }` takes
  16 bytes, not 24. `@repr(C)`, `@packed` and `@lang` structs keep
  declaration order. Codegen maps a declared field index to its slot in
  the GEP of a place and the `insertvalue` of an aggregate; the AIR keeps
  declared indices. Both stages do this, with the same IR.
- String literals are private constant globals.
- Vtables are constant globals: `{ drop, size, align, type_id, methods... }`.

Generated IR must be deterministic (thanks in part to the insertion-ordered,
unseeded `Map` of spec 2.6, the compiler may iterate maps freely):
identical inputs produce byte-identical
`.ll` files (required for the self-hosting check).

## 12. Linking

The driver writes `out.ll` to a build directory and invokes:

```
clang -O2 out.ll <runtime>/libofir_rt.a -o program      # release
clang -O0 -g out.ll <runtime>/libofir_rt.a -o program   # debug
```

Programs are split into codegen units, one per source module, compiled
by parallel clang processes and cached by the hash of their text
**[planned]** (ADR 0033, roadmap 6.6).

## 13. Diagnostics

```
error[E0600]: use of moved value `inv`
  --> examples/ownership.of:27:5
   |
25 |     discard(inv)
   |             --- value moved here
26 |
27 |     inv.add("potion")
   |     ^^^ value used here after move
   |
   = help: call `inv.clone()` before moving it if you need both copies
```

- Every error has a stable code (`E0001`...) documented in
  [`docs/errors/`](../errors/README.md), with a longer explanation (`ofir explain E0600`).
- Errors are sorted by position and deduplicated.
- A machine-readable JSON output (`--message-format=json`) feeds editors
  and the language server.

## 14. Performance targets

Measured and enforced as described in [performance.md](performance.md).

| Metric | Target |
|--------|--------|
| Front end (lex → borrow check) | ≥ 200k lines/second per core |
| Self-compile, debug build | < 5 seconds |
| Generated code | within 10% of equivalent C compiled with `clang -O2` |
| Hello world binary (stripped) | < 50 KB |

## 15. Planned evolutions

- Incremental front end: cache typed and lowered modules keyed by content
  hash (the back end is cached by ADR 0033).
- Parallel front end per module.
- Ofir-level optimizations on AIR before LLVM (inlining of trivial
  functions, removal of redundant checks) to reduce LLVM work.
- A direct machine-code backend for fast debug builds.
