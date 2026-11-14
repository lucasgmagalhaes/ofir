# 0067. What std and the tools do on each operating system

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-52 asks what each tier 1 and tier 2 target (ADR 0065) supports:
paths, text encoding, processes, the file system, networking, debugging
and packages, and which tests show it.

- Ofir strings are UTF-8. On Linux and macOS the C library takes and
  gives bytes, so UTF-8 names reach the system as they are.
- On Windows the runtime called the ANSI functions (`open`, `_mkdir`,
  `MoveFileExA`, `CreateProcessA`, `getenv`, the C runtime's `argv`).
  They convert through the ANSI code page, so a name outside it (`日本`
  on a Western system) could not be opened, passed or read.
- `std.path` already treats `\` as a separator and `C:` as a root on
  Windows, and the runtime turns `\` into `/` in the paths it returns.

## Decision

1. **Encoding.** Every name that crosses the runtime is UTF-8 on every
   target. On Windows the runtime converts to UTF-16 and calls the wide
   functions (`_wopen`, `_wstat`, `_wmkdir`, `MoveFileExW`, `_wopendir`,
   `_wgetenv`, `CreateProcessW`, `GetModuleFileNameW`); the arguments
   come from `CommandLineToArgvW`, and the console output code page is
   UTF-8. Standard streams are bytes on every target: no newline
   translation.
2. **Matrix.**

   | Area | Linux, macOS | Windows |
   |------|--------------|---------|
   | Paths | `/` separates; root `/` | `/` and `\` separate; roots `/`, `C:`, `C:\`; returned paths use `/` |
   | Names | bytes, UTF-8 by convention | UTF-8, converted to UTF-16 |
   | Arguments, environment | bytes | UTF-8 from the UTF-16 command line and environment |
   | Processes | `posix_spawnp`, searched in `PATH` | `CreateProcessW`, arguments quoted as the C runtime splits them; `.exe` added by the linker |
   | `os.abort()` | status 134 in a POSIX shell | status 3 |
   | File system | `std.fs` in full | `std.fs` in full; `rename` replaces an existing file |
   | Temporary files | `TMPDIR`, else `/tmp` | `TMPDIR`, else `TEMP` |
   | Networking | none: std has no `std.net` yet | the same |
   | Debugging | DWARF (`-g`); gdb checked by `tools/check_debugger.py` | DWARF; gdb when the toolchain has it |
   | Packages | git dependencies through the `git` program | the same |

3. **Tests.** `tests/run/system` runs on every tier 1 target in CI;
   `unicode.of` writes, renames, lists and removes non-ASCII names and
   passes non-ASCII arguments to a child. With `--target`, a test's
   `requires: unix` or `requires: windows` follows the target, so the run
   tests also pass for `x86_64-w64-windows-gnu` under Wine (with
   `OFIR_RUNNER=wine` and a UTF-8 locale) and for `aarch64-linux-gnu`
   under qemu-user.
4. **Tier 2.** `aarch64-linux-gnu` and `x86_64-apple-darwin` share the
   Linux and macOS column; `make test-cross` runs the first under
   qemu-user in CI.

## Consequences

- A program that handles names outside ASCII behaves the same on every
  tier 1 target.
- The runtime's Windows file and process code goes through `wide.c`; a
  new call there uses a wide function.
- Networking stays outside std until a later item adds it; its row in
  the matrix changes then.
