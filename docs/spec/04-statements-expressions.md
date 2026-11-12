# 4. Statements and expressions

## 4.1 Blocks

A block `{ ... }` is a sequence of statements and opens a new scope. When a
block is used as a value (in an `if` or `match` expression), its value is its
last expression.

## 4.2 Assignment

```ofir
x = 10
x += 1
user.age = 31
items[0] = 5
```

The left side must be a mutable place: a `let` binding, a `mut` parameter,
a field or element of a mutable place, or a place reached through a `&mut`
reference.
Assignment is a statement, not an expression.

## 4.3 `if`

```ofir
if x > 0 {
    println("positive")
} else if x < 0 {
    println("negative")
} else {
    println("zero")
}

const sign = if x >= 0 { 1 } else { -1 }
```

The condition must be `bool` (no truthiness). When `if` is used as a value,
`else` is mandatory and both branches must have the same type.

## 4.4 `for`

`for` is the only loop.

```ofir
for {                      // infinite loop
    if done() {
        break
    }
}

for n < 100 {              // while-style
    n *= 2
}

for i in 0..10 { }         // 0, 1, ..., 9
for i in 0..=10 { }        // 0, 1, ..., 10

for item in list { }       // borrows each element (Copy elements are copied)
for mut item in list { }   // borrows each element mutably; `list` must be mutable
for i, item in list { }    // with the index (int)

for key, value in map { }  // borrows keys and values, in insertion order

for word in text.splitWhitespace() { }   // an iterator: `next()` until `None`
```

A value whose type has `fn next(mut self) Option<T>` is an iterator
(`Iterator<T>`, chapter 9): the loop calls `next` until it returns `None`,
and the variable owns each item, which is dropped at the end of its
iteration unless the body moves it. A temporary iterator lives until the
end of the loop; an iterator in a variable is advanced in place, so the
variable must be a `let`, and the loop leaves it where it stopped. Stage 0
recognizes the method by its name and signature, until interfaces
(roadmap 5.1).

`break` and `continue` apply to the innermost loop. A loop with a label,
`name: for`, can be left from the loops inside it: `break name` ends that
loop and `continue name` starts its next iteration. The values of the
loops left are dropped first. A label names an enclosing loop, and an
enclosing loop cannot already use it.

```ofir
rows: for row in grid {
    for cell in row {
        if cell == target {
            break rows
        }
    }
}
```

## 4.5 `match`

```ofir
const area = match shape {
    Shape.Circle(r) => 3.14159 * r * r
    Shape.Rect(w, h) => w * h
    Shape.Empty => 0.0
}

match code {
    200 => println("ok")
    301 | 302 => println("redirect")
    400..=499 => println("client error")
    _ => println("other")
}
```

- Arms are separated by newlines. An arm's body is an expression or a block.
- Patterns: literals, ranges (`a..=b`), `_`, bindings (`x`), enum variants
  with sub-patterns (`Option.Some(x)`), struct patterns (`Point{x, y: 0}`) and
  alternatives (`p | q`).
- A guard may follow a pattern: `Option.Some(n) if n > 0 => ...`
- `match` **must be exhaustive**; a missing case is a compile error that
  shows a value no arm matches (`Result.Err(SizeError.TooLarge)`). An arm
  that the arms before it already cover is a warning (W0004).
- Matching an owned value moves its fields into the bindings.
- Matching a place the function does not own (a borrowed parameter, a field
  or element of one) borrows the matched parts instead; Copy parts are
  copied. See chapter 5.5.

## 4.6 `if const` and `const ... else`

`if const` is a shorthand for a `match` with one interesting arm:

```ofir
if const Option.Some(user) = findUser(id) {
    println("{user.name}")
} else {
    println("not found")
}
```

`const ... else` binds a refutable pattern for the **rest of the block**, or
runs the `else` block, which must leave the block (`return`, `break`,
`continue` or `panic`):

```ofir
fn greet(id: UserId) Result<str, str> {
    const Option.Some(user) = findUser(id) else {
        return Result.Err("user {id.0} not found")
    }
    const Role.Admin(level) = user.role else {
        return Result.Ok("hello, {user.name}")
    }
    return Result.Ok("welcome back, admin level {level}")
}
```

It keeps the main path of a function unindented while every failure case is
still handled explicitly. The `else` block has type `!` (chapter 2.15); a
block that can fall through is an error (`E0307`).

## 4.7 `return`, `break`, `continue`

```ofir
return            // in functions returning ()
return value
```

## 4.8 Operators and precedence

From highest to lowest precedence:

| Precedence | Operators | Associativity |
|-----------:|-----------|---------------|
| 9 | `.` field/method, `()` call, `[]` index, `?` | left |
| 8 | unary `-`, `!`, `&`, `&mut`, `*` (deref) | right |
| 7 | `as` | left |
| 6 | `*`, `/`, `%`, `<<`, `>>`, `&` | left |
| 5 | `+`, `-`, `\|`, `^` | left |
| 4 | `==`, `!=`, `<`, `<=`, `>`, `>=` | none |
| 3 | `&&` | left |
| 2 | `\|\|` | left |
| 1.5 | `??` | right |
| 1 | `..`, `..=` | none |

Comparison operators do not chain: `a < b < c` is a compile error.

**`??` (default for `Option`).** `a ?? b` is the value inside `a` if it is
`Some`, otherwise `b`. `b` is evaluated only when needed and must have the
type of the content, or be another `Option` of it (then the result is an
`Option`). It is right-associative, so defaults chain:

```ofir
const name = user.nickname ?? user.firstName ?? "anonymous"
const port = env("PORT") ?? "8080"
```

For a `Result`, use `r.unwrapOr(default)` or `?`. There is no `?.`
operator; inside functions returning `Option`, `?` already short-circuits:
`const city = user.address?.city`.

Bitwise operators sit at the same level as the matching arithmetic
operators, as in Go, so `a & mask == 0` means `(a & mask) == 0`.

Arithmetic and comparison operators require both operands to have the same
type. On `str`, `a + b` reads both operands without consuming them and
returns a new `str`; `s += t` appends `t` to `s` in place.
`==` and `!=` work on primitives, `str`, and on structs and enums whose
fields all support `==`.

## 4.9 Indexing

`list[i]`, `array[i]` and `s[i]` (bytes of a string) are bounds checked and
panic when `i` is out of range. The index type is `int`; a negative index is out of range and panics. `list.get(i)` returns `Option<&T>` instead of panicking.

Slicing (`list[a..b]`, `s[a..b]`) produces a borrowed view (chapter 2.12).

## 4.10 Order of evaluation

Operands, arguments, the fields of a struct literal and the elements of a
list or tuple literal are evaluated left to right. Struct literal fields
are evaluated in the order they are written, not in the order of the
struct declaration. An argument that is itself a call is evaluated, with
its own arguments, before the next argument.
