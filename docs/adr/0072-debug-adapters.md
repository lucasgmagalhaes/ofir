# 0072. Editors debug Ofir through the existing DAP adapters

- Status: Accepted
- Date: 2026-11-14

## Context

OFIR-60 asks that an editor can launch and attach to an Ofir program,
set breakpoints, and show the stack and the variables, on top of the
debug information of OFIR-36 (roadmap 6.25f).

Editors talk to debuggers through the Debug Adapter Protocol (DAP).
Two adapters that read DWARF are widely installed: gdb's built-in one
(`gdb -i=dap`, gdb 14 and later) and LLVM's `lldb-dap` (formerly
`lldb-vscode`). A program built with `-g` already describes its
functions, lines, parameters, locals and their types in DWARF.

## Decision

1. **No Ofir adapter.** Ofir does not ship a debug adapter or a
   debugger. `ofir build -g` writes the DWARF those adapters read; an
   editor is configured to run one of them on the executable.
2. **What is supported.** Launch, attach by process id, line and
   function breakpoints, `continue` and `next`, the stack with the
   source and line of each frame, and the arguments and locals with
   their values, a struct or a `str` expanding to its fields.
3. **Checked.** `tools/check_dap.py`, run by `make test-stage1`, speaks
   DAP to each adapter it finds, as an editor would, on a launched and
   on an attached program. CI runs it with gdb; the attach part needs a
   system that lets a process attach to one that is not its child
   (`kernel.yama.ptrace_scope` 0 on Linux).
4. **Adapter differences** the check found, and editors meet:
   - gdb 15 starts the program on `launch` instead of waiting for
     `configurationDone`, so breakpoints sent after `launch` race the
     program; an editor sends them before `launch`, or uses gdb 16 or
     `lldb-dap`.
   - gdb stops a process it attaches to and `lldb-dap` resumes it.
   - `lldb-dap` 18 loses an attach to a process whose standard output
     is a pipe.

## Consequences

- Editor support comes for free with each adapter and editor that
  speaks DAP, and improves with them.
- The adapters present Ofir values with C's rules: an enum, a tuple or
  an array shows as a struct with no fields (docs/design/toolchain.md,
  "Debug info"). Printing them as Ofir values would need a pretty
  printer per adapter, which is left for later.
- Debugging needs gdb or LLDB on the machine; Windows, where neither is
  the usual debugger, is not checked.
