# 6. Error handling

Ofir separates two kinds of failure:

- **Recoverable errors** (file not found, invalid input) are values of type
  `Result<T, E>` and must be handled.
- **Bugs** (index out of bounds, overflow, failed assertion) cause a
  **panic**, which stops the program.

There are no exceptions.

## 6.1 `Result` and `Option`

Both are built-in generic enums, behaving as if declared as:

```ofir
enum Result<T, E> {
    Ok(value: T)
    Err(error: E)
}

enum Option<T> {
    Some(value: T)
    None
}
```

`Option` and `Result` are in scope everywhere; like any variant, theirs are
named through the enum, in expressions and in patterns (`Option.Some(x)`,
`Option.None`, `Result.Ok(v)`, `Result.Err(e)`). They are declared in
`std/core/option.of` and `result.of`, marked `@lang("option")` and
`@lang("result")`.

```ofir
fn parsePort(s: str) Result<u16, str> {
    const n = parseInt(s)?
    if n < 1 || n > 65535 {
        return Result.Err("port out of range: {n}")
    }
    return Result.Ok(n as u16)
}
```

## 6.2 The `?` operator

`expr?` unwraps a `Result` or `Option`:

- on `Result.Ok(v)` / `Option.Some(v)`, the expression evaluates to `v`;
- on `Result.Err(e)` / `Option.None`, the enclosing function returns
  immediately with `Result.Err(e)` / `Option.None`.

The function's return type must be compatible: `?` on a `Result<T, E>`
requires the function to return `Result<U, E>`, and `?` on an `Option<T>`
requires it to return `Option<U>`.

If the error types differ, `?` widens the error when `F` is a union type
that contains `E` (chapter 17.3), and otherwise converts it when the
function's error type `F` satisfies `From<E>`: it declares
`fn from(value: move E) Self`, and `F.from(e)` is called. Otherwise the
conversion is explicit with `.mapErr(...)`. Stage 0 requires both error
types to be equal.

```ofir
struct AppError {
    message: str

    fn from(value: move ParseError) AppError {
        return AppError{message: "cannot parse: {value.text}"}
    }
}

fn load(text: str) Result<Config, AppError> {
    const n = parseInt(text)?    // a ParseError becomes an AppError
    ...
}
```

## 6.3 Unused results

Ignoring a value of type `Result` is a compile error:

```ofir
writeFile(path, data)        // error: unused Result
const _ = writeFile(path, data) // explicitly ignored
```

## 6.4 Helper methods

| Method | Behavior |
|--------|----------|
| `r.unwrap()` | the value, or panic with the error |
| `r.expect("msg")` | the value, or panic with `msg` |
| `r.unwrapOr(default)` | the value, or `default` |
| `r.isOk()` / `o.isSome()` | `bool` |
| `r.mapErr(f)` | convert the error (stage 1: needs closures) |

## 6.5 Panics

A panic prints a message and the source location, then terminates the
process with exit status 101. Destructors are **not** run on panic.

```
panic: index out of bounds: the length is 3 but the index is 5
  at src/main.of:12:17
```

Panics are raised by:

- out-of-bounds indexing;
- integer overflow, division by zero, invalid shifts;
- failed `as` conversions;
- `unwrap`/`expect` on `None` or `Err`;
- `assert(cond)` (and the other asserts of `std.testing`) and `panic("message")`;
- stack overflow (detected by the runtime);
- allocation failures (see below).

Allocation follows one policy everywhere (std containers, `Box`, strings,
the runtime):

- a size in bytes that does not fit in an `int`, or that is negative,
  panics with `capacity overflow` before anything is allocated;
- an allocation the system cannot provide panics with `out of memory`;
- a zero-sized value needs no storage: a `List` of them never holds more
  than its bookkeeping, and allocating zero bytes returns a valid block
  that is freed like any other.

Allocation is infallible: there is no API that returns an error instead
of panicking. Fallible allocation needs an explicit design (ADR 0048).

The stack is a resource, not part of the semantics: an optimization may
use less of it (ADR 0044 turns a recursive call into a jump), so a program
that overflows the stack without optimizations may finish with them.

Panics cannot be caught. Recovering from panics is intentionally not
supported.
