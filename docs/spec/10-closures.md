# 10. Closures

A closure is an anonymous function that can capture variables from the
scope where it is created. Function types (`fn(A) R`, `move fn(A) R`) are
in 10.2; the closures themselves are written as arrow functions (10.1).

## 10.1 Syntax

Closures are arrow functions, as in JavaScript (ADR 0023):

```ofir
const names = users.map(u => u.name.clone())      // one parameter, types inferred
users.sortBy((a, b) => a.age - b.age)           // several parameters
test(arg => println(arg))
const add = (a: int, b: int) int => a + b         // written types
const double = (x: int) int => {                  // block body
    return x * 2
}
const greet = move () => println("hi {name}")     // captures by value
```

```ebnf
Closure      = [ "move" ] ClosureHead "=>" ( Expr | Block ) .
ClosureHead  = identifier
             | "(" [ ClosureParam { "," ClosureParam } [ "," ] ] ")" [ Type ] .
ClosureParam = identifier [ ":" [ Mode ] Type ] .
```

- One parameter without a type may go without parentheses: `x => x + 1`.
  No parameter, several, or written types need them: `() => 0`,
  `(a, b) => a + b`, `(x: int) => x`.
- Parameter and result types can be omitted when they can be inferred from
  the expected function type; the result type comes after `)`, as in a
  function signature: `(x: int) int => x * 2`. A parameter whose type is
  neither written nor inferred is E0336.
- The body is an expression or a block. In a block, `return` returns from
  the closure.
- A closure is recognized by looking ahead: an identifier followed by `=>`,
  or a parenthesized list whose `)` is followed by `=>` or by a type and
  `=>`. Anything else in parentheses is a parenthesized expression or a
  tuple.
- In a `match` guard (`pattern if guard => body`), a closure must be in
  parentheses: the first `=>` ends the guard.
- The newline rule (chapter 1.3) lets the body start on the next line
  after `=>`.

## 10.2 Two function types

| Type | Meaning | Second-class? | Copy? |
|------|---------|:-------------:|:-----:|
| `fn(A) R` | a **borrowed** function value | yes | yes |
| `move fn(A) R` | an **owned** function value | no | no (move) |

- `fn(A) R` behaves like a reference (chapter 5.4): it can be called, bound
  locally and passed as an argument, but **not stored in a struct or
  container, and not returned**. Every function and every closure converts
  to it. This is the type of callbacks: `map`, `filter`, `sortBy`, `each`.
- `move fn(A) R` owns its captured values. It can be stored, returned and
  sent across the program. Only named functions and `move` closures convert
  to it. It is a move type; dropping it drops its captured values.

A parameter declared `handler: move fn()` takes ownership of an owned
function value: `move` is both the parameter mode and part of the type.

Because function-typed parameters are borrowed by default, a function that
receives a callback can never store it by accident; one that needs to store
it says so in its signature:

```ofir
fn each(xs: List<int>, f: fn(int)) { ... }            // cannot keep `f`

fn onClick(b: mut Button, handler: move fn()) {      // keeps `handler`
    b.handlers.push(handler)
}
```

Storing or returning a `fn(A) R` is E0702, checked by the self-hosted
compiler (stage 0 accepts it).

A function value in a field is called with the method call syntax:
`x.f(args)` calls the value of field `f` when the type of `x` has no method
`f`. A method of the same name takes precedence, and parentheses do not
change that: `(x.f)(args)` is the same call. A call through a field reads
the field; it does not move the function value out of `x`. Stage 0 does not
accept such calls.

```ofir
struct Button {
    label: str
    onClick: move fn() int
}

fn press(b: &Button) int {
    return b.onClick()      // calls the field: `Button` has no method `onClick`
}
```

## 10.3 Captures

A non-`move` closure captures each variable it uses by reference, chosen
from how the body uses it:

| Use in body | Capture |
|-------------|---------|
| only read | shared reference `&` |
| modified | exclusive reference `&mut` |

```ofir
let count = 0
const increment = () => {   // captures `count` by &mut
    count += 1
}
increment()
increment()
println("{count}")                      // 2: the borrow ended at the last call
```

The captured borrows follow the borrowing rules of chapter 5: while
`increment` is alive (until its last use), `count` cannot be used directly.

A `move` closure captures each variable it uses **by value**: Copy values
are copied, other values are moved into the closure. Captured values are
**immutable** inside a `move` closure (E0337); mutable state that must
outlive a call belongs in a struct with methods.

```ofir
fn makeGreeter(name: move str) move fn() str {
    return move () str => "hello, {name}"
}
```

## 10.4 Representation

- A function value is a pair: a code pointer and an environment pointer.
  Named functions use a null environment.
- A non-`move` closure stores its captured references in an environment on
  the creator's stack: no allocation.
- A `move` closure stores its captured values in a heap-allocated
  environment, freed when the closure is dropped. Closures capturing nothing
  do not allocate.
- When a function receiving a callback is inlined (or generic and
  monomorphized), a closure passed directly to it is inlined too.
- A `move fn` is `Send` (chapter 14) when all its captured values are `Send`.
