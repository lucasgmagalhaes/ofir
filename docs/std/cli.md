# `std.cli`

Command-line programs (ADR 0060): arguments, help, shell completions, exit
codes and color.

## Arguments

A `Command` declares flags, options and positionals; `parse` reads the
arguments without the program name. The forms are `--name`,
`--name=value`, `--name value`, grouped short flags `-abc`, `-ovalue`,
`-o value` and `--`, after which every argument is a positional. `-` alone
is a positional. Long options are not abbreviated.

| Item | Description | S0 |
|------|-------------|:--:|
| `fn Command.new(name: str, about: str) Command` | a command with a one-line summary | ✓ |
| `fn setVersion(mut self, version: str)` | adds `--version` and `-V` | ✓ |
| `fn flag(mut self, long: str, short: str, help: str)` | no value; `short` is one character or `""` | ✓ |
| `fn option(mut self, long: str, short: str, value: str, help: str)` | takes a value; `value` is its placeholder, such as `FILE` | ✓ |
| `fn withDefault(mut self, value: str)` | a default for the option added last | ✓ |
| `fn positional(mut self, name: str, help: str, required: bool)` | in declaration order | ✓ |
| `fn rest(mut self, name: str, help: str)` | any number of positionals after the declared ones | ✓ |
| `fn parse(self, args: &[str]) Result<Matches, CliError>` | | ✓ |
| `fn parseOrExit(self, args: &[str]) Matches` | prints help or the version and exits with 0; prints a usage error to stderr and exits with `EXIT_USAGE` | ✓ |
| `struct CliError { kind: ErrorKind, message: str }` | `ErrorKind` is `Help`, `Version` or `Usage` | ✓ |
| `fn flag(self, long: str) bool`, `fn count(self, long: str) int` | on `Matches`: given at all, and how many times (`-vvv`) | ✓ |
| `fn value(self, long: str) Option<str>`, `fn values(self, long: str) List<str>` | the last value or the default, and every value | ✓ |
| `fn positional(self, name: str) Option<str>`, `fn rest(self) List<str>` | | ✓ |

An unknown long option at most two edits from a known one gets a
suggestion: ``unknown option `--forse`; did you mean `--force`?``.

## Help and completions

| Item | Description | S0 |
|------|-------------|:--:|
| `fn help(self) str` | the summary, the usage line, then arguments and options in aligned columns, with defaults | ✓ |
| `fn usageLine(self) str` | `Usage: name [OPTIONS] <REQUIRED> [OPTIONAL] [REST]...` | ✓ |
| `fn completion(self, shell: Shell) str` | a script for `Shell.Bash`, `Shell.Zsh` or `Shell.Fish` that completes the option names | ✓ |

## Exit codes and color

| Item | Description | S0 |
|------|-------------|:--:|
| `const EXIT_FAILURE: int = 1`, `const EXIT_USAGE: int = 2` | | ✓ |
| `fn fail(message: str) !` | prints `error: message` to stderr and exits with `EXIT_FAILURE` | ✓ |
| `enum Style` | `Bold`, `Dim`, `Red`, `Green`, `Yellow`, `Blue`, `Magenta`, `Cyan` | ✓ |
| `fn paint(text: str, style: Style, enabled: bool) str` | wraps the text in ANSI escapes when enabled | ✓ |
| `fn colorEnabled(stream: io.Stream) bool` | `colorChoice` on `NO_COLOR`, `CLICOLOR_FORCE`, `TERM` and `io.isTerminal` | ✓ |
| `fn colorChoice(noColor: Option<str>, force: Option<str>, term: Option<str>, terminal: bool) bool` | no color when `NO_COLOR` is not empty; color when `CLICOLOR_FORCE` is neither empty nor `0`; no color when `TERM` is `dumb`; else color on a terminal | ✓ |

```ofir
import std.cli
import std.io
import std.os

fn main() {
    let cmd = cli.Command.new("greet", "Print a greeting")
    cmd.flag("loud", "l", "Shout it")
    cmd.option("name", "n", "NAME", "Who to greet")
    cmd.withDefault("world")
    const all = os.args()
    const m = cmd.parseOrExit(all.slice(1, all.len()))
    const name = m.value("name") ?? ""
    const text = if m.flag("loud") { "HELLO, {name}!" } else { "Hello, {name}." }
    println(cli.paint(text, cli.Style.Bold, cli.colorEnabled(io.Stream.Stdout)))
}
```
