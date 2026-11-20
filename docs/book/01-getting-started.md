# 1. Getting started

## Installing

A release is one archive per target, `ofir-<version>-<target>.tar.gz`,
next to its checksum file `ofir-<version>-<target>.tar.gz.sha256`. The
target is the one of your machine, such as `x86_64-linux-gnu`,
`aarch64-apple-darwin` or `x86_64-w64-windows-gnu`. Ofir needs clang 17
or newer on the `PATH` to build programs.

Download both files of version `0.5.0` for your target, then take the
installer out of the archive and give it the archive. It checks the
archive against its checksum before it writes anything:

```sh
tar -xzf ofir-0.5.0-x86_64-linux-gnu.tar.gz ofir-0.5.0/install.sh
sh ofir-0.5.0/install.sh ofir-0.5.0-x86_64-linux-gnu.tar.gz
export PATH="$HOME/.ofir/bin:$PATH"
ofir version
```

`ofir version` prints `ofir 0.5.0` and the prefix it runs from. The installer
keeps each version in `~/.ofir/toolchains/<version>`: `--list` shows
them, `--use <version>` makes another one active, and `--uninstall`
removes Ofir. On Windows, run the same commands in Git Bash, with a
MinGW-w64 toolchain next to clang (see the
[toolchain guide](../design/toolchain.md#windows)).

## Hello, world

Save this program as `hello.of`:

<!-- check: run -->
```ofir
fn main() {
    const name = "Ofir"
    println("Hello, {name}!")
}
```

and run it:

```sh
ofir run hello.of
```

```text
Hello, Ofir!
```

`main` is where the program starts. `const` names a value that does not
change, and `{name}` inside a string inserts the value of an
expression. `ofir build hello.of -o hello` writes an executable instead
of running it, and `ofir check hello.of` reports errors without
building anything.

## When something goes wrong

Each error has a code. `ofir explain` prints what it means, with an
example and its fix:

```sh
ofir explain E0600
```

The next chapters meet the most common codes on purpose.
