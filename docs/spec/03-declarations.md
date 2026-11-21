# 3. Declarations

## 3.1 Variables

```ofir
const name = "Ofir"       // immutable binding
let count = 0             // mutable binding
let total: float = 0      // explicit type
let buffer: List<u8>      // must be assigned before its first use
```

- `const` bindings cannot be reassigned, and the value they own cannot be
  mutated through them.
- `let` bindings can be reassigned and mutated.
- In a function, `const` takes any value and may bind a pattern
  (chapter 4); `let` binds a name only (ADR 0031).
- A variable must be definitely initialized before it is read. A declaration
  without a value (`let x: int`) is allowed only if every path assigns it
  before its first use. There are no implicit zero values.
- A name may shadow a name from an enclosing scope, but not one declared in
  the same scope. `_` names nothing: it may stand for any number of
  parameters or loop variables of one scope (`fn f(_: int, _: int)`,
  `for _, _ in list`).

## 3.2 Constants

```ofir
const MAX_USERS = 1024
const GREETING: str = "hello"
```

Constants are evaluated at compile time. Their initializer may only use
literals, other constants and arithmetic operators.

At module level, `const` declares a constant and `let` a mutable global,
whose use requires `unsafe` (chapter 11).

## 3.3 Functions

```ofir
fn add(a: int, b: int) int {
    return a + b
}

fn log(message: str) {          // returns ()
    println(message)
}
```

```ebnf
FnDecl = [ "pub" ] "fn" identifier "(" [ Params ] ")" [ Type ] Block .
Params = Param { "," Param } [ "," ] .
Param  = identifier ":" Type .
```

- The return type follows the parameter list directly, as in Go:
  `fn name() str`. A function with no return type returns `()`.
- Parameters are borrowed by default; `mut` and `move` change how they are
  passed (chapter 5.3). The caller never marks the mode:

  ```ofir
  fn rename(user: mut User, name: move str) {
      user.name = name
  }

  rename(ana, "Ana Maria")
  ```
- A function with a non-`()` return type must `return` on every path; the
  compiler rejects functions where control can reach the end of the body.
- The order of declarations in a file does not matter.
- **There is no overloading**: two functions in the same scope (or two
  methods of the same type) cannot have the same name, whatever their
  parameters. Use distinct names (`parseInt`, `parseFloat`), default
  values, or generics.

### Default values and named arguments

Trailing parameters may have a default value. Any argument may be passed by
name, after the positional ones:

```ofir
fn connect(host: str, port: int = 443, timeout: int = 30, verbose: bool = false) Conn { ... }

connect("example.com")
connect("example.com", 8443)
connect("example.com", timeout: 5, verbose: true)
```

- Once a parameter has a default, all following parameters must have one.
- A default value is a constant expression (chapter 3.2), an empty
  collection literal (`[]`), or a call to an associated function without
  arguments (`Map.new()`). It is evaluated at each call that omits it,
  after the arguments written, which are evaluated in the order written.
- Named arguments use the parameter's name and may come in any order, but
  only after all positional arguments; each parameter is given at most once.
  A parameter without a default must be given, positionally or by name.
- Parameter names are therefore part of a function's public API: renaming a
  `pub` function's parameter is a breaking change.
- A function value (`fn(A, B) R`, chapter 10) has no defaults and no names:
  calls through it pass every argument positionally.

### Entry point

Every program has exactly one `fn main()` in its root module. It takes no
parameters and returns `()` or `Result<(), E>` where `E` satisfies
`Display`. If `main` returns an `Err`, the error is printed to standard error
as `error: <message>` and the process exits with status 1. Command line
arguments are available through `std.os.args()`.

## 3.4 Structs

```ofir
struct User {
    name: str
    email: str
    age: int
}

const u = User{name: "Ana", email: "ana@example.com", age: 30}
const v = User{age: 31, ..u}     // copy/move the remaining fields from u
```

- Every field without a default value must be initialized in a struct
  literal.
- `pub` on the struct makes the type visible to other modules; `pub` on a
  field makes the field visible. Fields are private to the module by default.
- A struct may not contain itself by value, directly or indirectly; use
  `Box<T>` or a collection instead.

### Default field values

A field may declare a default value, used when a literal omits the field:

```ofir
struct Config {
    host: str = "localhost"
    port: int = 8080
    tags: List<str> = []
    verbose: bool = false
}

const a = Config{}                       // all defaults
const b = Config{port: 80, verbose: true}
```

The same rules as default parameter values apply: a constant expression, an
empty collection literal, or a call to an associated function without
arguments, evaluated each time it is used. A default cannot refer to other
fields.

### Tuple structs

A tuple struct has positional fields. With a single field it creates a new,
distinct type at zero cost (a "newtype"):

```ofir
copy struct UserId(int)
copy struct Rgb(u8, u8, u8)

const id = UserId(42)
const red = Rgb(255, 0, 0)
println("{id.0} {red.1}")
const n: int = id            // error: mismatched types: expected int, found UserId
```

Tuple structs are constructed by calling the type name, their fields are
accessed with `.0`, `.1`, ..., and they can be destructured in patterns:
`const UserId(raw) = id`. They may declare methods in a body after the fields:
`struct Meters(float) { fn km(self) float { return self.0 / 1000.0 } }`.

## 3.5 Enums

```ofir
enum Token {
    Number(value: int)
    Ident(name: str)
    Plus
    Minus
}

const t = Token.Number(42)
const p = Token.Plus
```

A variant is always named through its enum, in expressions and in
patterns: `Token.Plus`, never `.Plus` or a bare `Plus` (E0326).

## 3.6 Methods

Methods are declared **inside** the body of the struct or enum they belong
to, after its fields or variants. A method takes `self` as its first
parameter; the mode of `self` (chapter 5.3) says how the value is used:

```ofir
pub struct Point {
    pub x: float
    pub y: float

    pub fn origin() Point {                 // no `self`: associated function
        return Point{x: 0.0, y: 0.0}
    }

    pub fn length(self) float {             // reads the point
        return (self.x * self.x + self.y * self.y).sqrt()
    }

    pub fn scale(mut self, k: float) {      // modifies the point
        self.x *= k
        self.y *= k
    }

    pub fn intoVec2(move self) Vec2 {       // consumes the point
        return Vec2{x: self.x, y: self.y}
    }
}
```

```ofir
let p = Point.origin()
p.scale(2.0)
const l = p.length()
```

- Inside a method, fields and other methods are always reached through
  `self` (`self.x`, `self.length()`); there is no implicit `this`.
  Associated functions are called through the type name, or `Self` inside
  the type (`Self.origin()`).
- `self` has the type of the enclosing struct or enum, `Self`.
- `pub` on a method makes it visible to other modules; methods are private
  by default, like everything else.
- The type parameters of a generic type are in scope in all its methods
  (chapter 8.3).
- All the methods of a type are declared in its body, so they always live in
  the type's module.
- By convention fields come first, then associated functions, then methods;
  the formatter enforces this order.

Enums declare methods after their variants:

```ofir
enum Shape {
    Circle(radius: float)
    Rect(width: float, height: float)

    fn area(self) float {
        return match self {
            Shape.Circle(r) => 3.14159 * r * r
            Shape.Rect(w, h) => w * h
        }
    }
}
```

## 3.7 Destructors

A type may define a `drop` method, called automatically when a value of that
type goes out of scope (chapter 5):

```ofir
struct File {
    fd: i32

    fn drop(mut self) {
        close(self.fd)
    }
}
```

## 3.8 External functions

Functions implemented in C (libc or the Ofir runtime) are declared in an
`extern "C"` block and can only be called from `unsafe` code:

```ofir
extern "C" {
    fn write(fd: i32, buf: *u8, len: uint) int
}
```

See chapter 11 for the full rules.
