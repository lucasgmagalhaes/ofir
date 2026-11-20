# 5. Projects and tests

## A project

A program of more than one file is a project: a directory with a
manifest, `ofir.json`, next to its sources.

```jsonc
{
  "name": "greet",
  "version": "0.1.0",
  "build": {
    "kind": "exe",
    "root": "src/main.of",
  },
}
```

The manifest is JSON with comments and trailing commas. `build.root` is
the file with `main`; `ofir run src/main.of` builds and runs the
project, and the
[toolchain guide](../design/toolchain.md#ofirjson) lists the other
keys, among them `dependencies`.

## Tests

A function marked `@test` is a test. `ofir test` builds the tests of a
file and runs them; `assert` comes from `std.testing`:

<!-- check: test file=clamp.of -->
```ofir
import std.testing.assert

fn clamp(n: int, low: int, high: int) int {
    if n < low {
        return low
    }
    if n > high {
        return high
    }
    return n
}

@test
fn clampKeepsValuesInRange() {
    assert(clamp(5, 0, 10) == 5)
    assert(clamp(-1, 0, 10) == 0)
    assert(clamp(11, 0, 10) == 10)
}
```

```sh
ofir test clamp.of
```

```text
test clamp.clampKeepsValuesInRange ... ok

1 tests: 1 passed, 0 failed
```

A failed `assert` stops its test with `panic: assertion failed` and the
line of the `assert`; the other tests still run, and `ofir test` exits
with an error when any of them failed.

## Where to go next

- The [examples](../../examples) are small programs, and the
  applications of [`apps/`](../../apps/README.md) are complete ones
  that CI builds and tests.
- The [standard library](../std/README.md) documents each module.
- The [language specification](../spec/README.md) is the full
  reference.
