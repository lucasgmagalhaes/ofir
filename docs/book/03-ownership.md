# 3. Ownership

Ofir has no garbage collector. Each value has one owner, and the value
is freed when its owner goes out of scope. The compiler checks this:
the errors of this chapter are the ones it reports when a program
breaks the rules.

## Moving a value

A parameter marked `move` takes ownership of its argument. After the
call, the caller's name no longer holds a value:

<!-- check: error E0600 -->
```ofir
fn shout(text: move str) str {
    return text.toUpper()
}

fn main() {
    const greeting = "hello".toOwned()
    const loud = shout(greeting)
    println(loud)
    println(greeting)
}
```

```text
error[E0600]: use of moved value `greeting`
 --> main.of:9:13
  |
9 |     println(greeting)
  |             ^^^^^^^^
  |
  = help: `greeting` was moved before: clone it there if both copies are needed
```

The help names two fixes: pass `greeting.clone()`, or, since `shout`
only reads its text, drop `move` from the parameter. A parameter
without a mode borrows its argument for the call, and the caller keeps
it:

<!-- check: run -->
```ofir
fn shout(text: str) str {
    return text.toUpper()
}

fn main() {
    const greeting = "hello".toOwned()
    const loud = shout(greeting)
    println(loud)
    println(greeting)
}
```

```text
HELLO
hello
```

Nothing marks the call itself: the signature says whether a function
borrows or takes its argument. Small values such as `int`, `float` and
`bool` are `Copy`, and are copied instead of moved.

## Changing a value through a borrow

A `mut` parameter borrows its argument and may change it:

<!-- check: run -->
```ofir
fn addAll(total: mut List<int>, values: List<int>) {
    for v in values {
        total.push(v)
    }
}

fn main() {
    let numbers: List<int> = [1, 2]
    addAll(numbers, [3, 4])
    println("{numbers.len()} numbers")
}
```

```text
4 numbers
```

While a value is borrowed as mutable, nothing else may use it, so a
change cannot happen under a reader's feet:

<!-- check: error E0703 -->
```ofir
fn main() {
    let names: List<str> = ["ada".toOwned()]
    const all = &mut names
    println("{names.len()} names")
    all.push("grace".toOwned())
}
```

E0703 reports the use of `names` while `all` borrows it. Using `names`
after the last use of `all`, or changing it through `names` directly,
fixes it. Two mutable borrows of the same value at once are E0700, and
a borrow that outlives its value is E0706.

`ofir explain E0703` gives the example and the fix of each of these
codes.
