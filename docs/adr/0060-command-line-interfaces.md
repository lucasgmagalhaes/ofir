# 0060. Command-line parsing, help, completions and color in std.cli

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-45 asks for CLI and terminal facilities: arguments and help,
completions, standard streams, error exits and no-color behavior.

`std.os` gives the arguments, the environment and `exit`, and the prelude
prints to stdout and stderr. A program that wants options writes its own
loop over `os.args()`, its own help text and its own exit codes, and it
cannot tell whether a stream is a terminal, so it cannot decide on color.

## Decision

1. **`std.io` tells whether a standard stream is a terminal.**
   - `io.isTerminal(stream: Stream) bool` with `enum Stream { Stdin,
     Stdout, Stderr }`, backed by `isatty` (`_isatty` on Windows) in the
     runtime.
2. **`std.cli.Command` declares a command and parses its arguments.**
   - A command has a name, an optional version and a one-line summary.
     `flag`, `option` and `positional` add arguments, each with help text;
     `rest` takes the remaining positionals.
   - Syntax: `--name`, `--name=value`, `--name value`, `-n`, grouped short
     flags `-abc`, `-ovalue` and `-o value`. `--` ends the options, and
     `-` alone is a positional.
   - A repeated option keeps every value; `value` returns the last one and
     `values` all of them. An option may have a default.
   - `parse(args)` takes the arguments without the program name and
     returns `Matches` or a `CliError`. An unknown option, a missing value,
     a missing positional and an extra positional are usage errors, with a
     suggestion when an option is one edit from a known one.
   - `--help` and `-h` and, with a version, `--version` and `-V` are added
     to every command. They return a `CliError` of kind `Help` or `Version`
     holding the text to print.
3. **Errors exit with documented codes.**
   - `parseOrExit` prints help or the version to stdout and exits with 0,
     and prints a usage error and a hint to stderr and exits with 2
     (`EXIT_USAGE`), as BSD `sysexits` and most tools do.
   - `cli.fail(message)` prints `error: message` to stderr and exits with
     1 (`EXIT_FAILURE`).
4. **Help and completions come from the declaration.**
   - `help()` writes the usage line, the summary and aligned columns of
     arguments and options, with defaults.
   - `completion(shell)` writes a completion script for `Shell.Bash`,
     `Shell.Zsh` or `Shell.Fish` that completes the option names.
     Completing values (files, choices) waits for a later decision.
5. **Color follows the conventions of `NO_COLOR` and `CLICOLOR_FORCE`.**
   - `colorEnabled(stream)` is false when `NO_COLOR` is set and not empty,
     true when `CLICOLOR_FORCE` is set and not `0`, false when `TERM` is
     `dumb`, and otherwise whether the stream is a terminal.
     `colorChoice` applies the same rule to given values, for tests.
   - `paint(text, style, enabled)` wraps text in an ANSI SGR sequence
     only when `enabled` is true.
6. **Subcommands, prompts and terminal size stay open.**
   - Subcommands can be dispatched on the first positional for now.
   - Line editing, raw mode and window size need more runtime support and
     wait for a terminal module of their own.

## Consequences

- A program gets option parsing, help, `--version`, completions and the
  usual exit codes from one declaration.
- Color is off in pipes, in CI logs without `CLICOLOR_FORCE` and wherever
  the user set `NO_COLOR`.
- The parser does not abbreviate long options: a prefix is an unknown
  option, so adding an option never changes what an old command line
  means.
