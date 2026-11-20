# 4. Errors

A failure the program can recover from, such as invalid input or a
missing file, is a value of type `Result<T, E>`: `Result.Ok(value)` or
`Result.Err(error)`. A bug, such as an index out of bounds, is a panic,
which stops the program. There are no exceptions.

## Returning and handling errors

<!-- check: run -->
```ofir
fn parseAge(text: str) Result<int, str> {
    const n = match int.parse(text) {
        Result.Ok(n) => n
        Result.Err(_) => {
            return Result.Err("not a number: {text}")
        }
    }
    if n < 0 || n > 150 {
        return Result.Err("out of range: {n}")
    }
    return Result.Ok(n)
}

fn describe(text: str) Result<str, str> {
    const age = parseAge(text)?
    return Result.Ok("{age} years")
}

fn main() {
    for input in ["42", "-3", "ten"] {
        match describe(input) {
            Result.Ok(s) => println(s)
            Result.Err(e) => println("error: {e}")
        }
    }
}
```

```text
42 years
error: out of range: -3
error: not a number: ten
```

`int.parse` returns a `Result` too, and `match` handles both of its
cases. `?` is the short form for passing an error on: `parseAge(text)?`
is the age when parsing worked, and otherwise returns the error from
`describe` at once. It needs the function to return a `Result` with the
same error type.

## A result cannot be ignored

Dropping a `Result` without looking at it is an error, so a failure is
never lost silently:

<!-- check: error E0305 -->
```ofir
fn save(name: str) Result<int, str> {
    if name.isEmpty() {
        return Result.Err("empty name")
    }
    return Result.Ok(name.len())
}

fn main() {
    save("notes")
}
```

Handle it with `match` or `?`, or write `const _ = save("notes")` to
ignore it on purpose.
