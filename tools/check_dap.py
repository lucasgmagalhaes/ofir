#!/usr/bin/env python3
"""Checks that an editor can debug an Ofir program through the Debug
Adapter Protocol (OFIR-60, ADR 0072, docs/design/toolchain.md).

Ofir has no adapter of its own: a program built with `-g` carries DWARF,
and the adapters that already read DWARF serve it, `gdb -i=dap` (gdb 14
or later) and `lldb-dap` (LLVM's, formerly `lldb-vscode`). This script
speaks DAP to each one it finds, as an editor would:

- launch: a function and a line breakpoint stop, the stack names the
  frames with their source and line, the arguments and locals have
  their values, a struct and a `str` expand to their fields, `next`
  steps to the next line and the program exits with its code;
- attach: a running program is attached by pid, a breakpoint set after
  the attach stops in it, and the stack and locals read as above.

Skipped on Windows and when no adapter is installed; the attach part
runs on Linux when the system allows attaching to a process that is not
a child.

    python3 tools/check_dap.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from queue import Empty, Queue

PROGRAM = """\
struct Point {
    x: int
    y: i32
    ok: bool
}

fn twice<T>(value: move T, n: int) T {
    const doubled = n * 2
    print("{doubled}")
    return value
}

fn area(p: &Point, scale: f64) f64 {
    const w = p.x as f64 * scale
    return w
}

fn main() {
    let count = 41
    count += 1
    const pt = Point{x: 3, y: -4, ok: true}
    const a = area(&pt, 1.5)
    const name = "ofir"
    print("{twice(count, 5)}")
    print("{a}")
    print(name)
}
"""

# A program that runs long enough to be attached to.
WAITING = """\
import std.time

fn tick(round: int) int {
    const next = round + 1
    return next
}

fn main() {
    let round = 0
    for round < 3000 {
        round = tick(round)
        time.sleep(10000000)
    }
}
"""

TIMEOUT = 60


class Failure(Exception):
    pass


class Adapter:
    """A debug adapter process, spoken to with DAP messages over stdio."""

    def __init__(self, command: list[str]):
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.seq = 0
        self.messages: Queue = Queue()
        self.held: list[dict] = []
        threading.Thread(target=self.reader, daemon=True).start()

    def reader(self) -> None:
        stream = self.process.stdout
        while True:
            length = None
            while True:
                line = stream.readline()
                if not line:
                    self.messages.put(None)
                    return
                line = line.strip()
                if not line:
                    break
                key, _, value = line.partition(b":")
                if key.strip().lower() == b"content-length" and value.strip().isdigit():
                    length = int(value)
            if length is not None:
                self.messages.put(json.loads(stream.read(length)))

    def send(self, command: str, arguments: dict | None = None) -> int:
        self.seq += 1
        message = {"seq": self.seq, "type": "request", "command": command,
                   "arguments": arguments or {}}
        body = json.dumps(message).encode()
        self.process.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.process.stdin.flush()
        return self.seq

    def wait(self, wanted, what: str) -> dict:
        for index, message in enumerate(self.held):
            if wanted(message):
                return self.held.pop(index)
        while True:
            try:
                message = self.messages.get(timeout=TIMEOUT)
            except Empty:
                raise Failure(f"no {what} after {TIMEOUT}s")
            if message is None:
                raise Failure(f"the adapter exited before {what}")
            if wanted(message):
                return message
            self.held.append(message)

    def response(self, seq: int, command: str) -> dict:
        message = self.wait(
            lambda m: m.get("type") == "response" and m.get("request_seq") == seq,
            f"response to `{command}`")
        if not message.get("success"):
            raise Failure(f"`{command}` failed: {message.get('message')}")
        return message.get("body") or {}

    def request(self, command: str, arguments: dict | None = None) -> dict:
        return self.response(self.send(command, arguments), command)

    def event(self, name: str) -> dict:
        message = self.wait(lambda m: m.get("type") == "event" and m.get("event") == name,
                            f"`{name}` event")
        return message.get("body") or {}

    def close(self) -> None:
        try:
            self.send("disconnect", {"terminateDebuggee": True})
        except OSError:
            pass
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise Failure(message)


def frames(dap: Adapter, thread: int) -> list[dict]:
    return dap.request("stackTrace", {"threadId": thread})["stackFrames"]


def children(dap: Adapter, reference: int) -> dict[str, dict]:
    found = dap.request("variables", {"variablesReference": reference})["variables"]
    return {variable["name"]: variable for variable in found}


def variables(dap: Adapter, frame: dict) -> dict[str, dict]:
    """The arguments and locals of a frame, by name; registers left out."""
    found: dict[str, dict] = {}
    for scope in dap.request("scopes", {"frameId": frame["id"]})["scopes"]:
        name = scope["name"].lower()
        if "register" in name or "global" in name or "static" in name:
            continue
        found.update(children(dap, scope["variablesReference"]))
    return found


def fields(dap: Adapter, variable: dict) -> dict[str, str]:
    """The fields of a struct, or of the struct a reference points to."""
    found = children(dap, variable.get("variablesReference", 0) or 0)
    expect(bool(found), f"`{variable['name']}` has no fields")
    if len(found) == 1:
        (inner,) = found.values()
        if inner.get("variablesReference"):
            # gdb shows a reference as one child, the value it points to.
            nested = children(dap, inner["variablesReference"])
            if nested:
                found = nested
    return {name: child["value"] for name, child in found.items()}


def value(found: dict[str, dict], name: str) -> str:
    expect(name in found, f"no variable `{name}` in {sorted(found)}")
    return found[name]["value"]


def check_frame(frame: dict, name: str, line: int) -> None:
    where = f"{frame.get('name')} at line {frame.get('line')}"
    expect(name in frame.get("name", ""), f"the frame is {where}, not `{name}`")
    expect(frame.get("line") == line, f"the frame is {where}, not line {line}")
    path = (frame.get("source") or {}).get("path", "")
    expect(path.endswith("main.of"), f"the frame of `{name}` has the source {path!r}")


def stopped(dap: Adapter, reason: str) -> int:
    body = dap.event("stopped")
    expect(body.get("reason") == reason, f"stopped for {body.get('reason')}, not {reason}")
    return body.get("threadId", 1)


def configure(dap: Adapter, source: Path, lines: list[int], functions: list[str]) -> None:
    if lines:
        body = dap.request("setBreakpoints", {
            "source": {"path": str(source)},
            "breakpoints": [{"line": line} for line in lines]})
        expect(len(body["breakpoints"]) == len(lines), "a line breakpoint is missing")
    if functions:
        body = dap.request("setFunctionBreakpoints", {
            "breakpoints": [{"name": name} for name in functions]})
        expect(len(body["breakpoints"]) == len(functions), "a function breakpoint is missing")
    dap.request("configurationDone")


def start(dap: Adapter, adapter: dict, command: str, arguments: dict, source: Path,
          lines: list[int], functions: list[str]) -> None:
    """initialize, then launch or attach and set the breakpoints.

    An editor sends `launch` and configures on the `initialized` event;
    gdb 15 sends that event before `launch` and starts the program on
    `launch`, so with it the breakpoints go first."""
    dap.request("initialize", {"clientID": "check_dap", "adapterID": "ofir",
                               "linesStartAt1": True, "columnsStartAt1": True,
                               "pathFormat": "path"})
    if adapter["configureFirst"]:
        dap.event("initialized")
        configure(dap, source, lines, functions)
        dap.request(command, arguments)
    else:
        seq = dap.send(command, arguments)
        dap.event("initialized")
        configure(dap, source, lines, functions)
        dap.response(seq, command)


def check_launch(adapter: dict, exe: Path, source: Path) -> None:
    dap = Adapter(adapter["command"])
    try:
        start(dap, adapter, "launch", {"program": str(exe), "cwd": str(exe.parent)},
              source, [25], ["area"])
        thread = stopped(dap, "breakpoint")
        stack = frames(dap, thread)
        check_frame(stack[0], "area", 14)
        check_frame(stack[1], "main", 22)
        found = variables(dap, stack[0])
        expect(value(found, "scale") == "1.5", f"scale = {value(found, 'scale')}")
        point = fields(dap, found["p"])
        expect(point.get("x") == "3" and point.get("y") == "-4" and point.get("ok") == "true",
               f"*p = {point}")

        dap.request("continue", {"threadId": thread})
        thread = stopped(dap, "breakpoint")
        stack = frames(dap, thread)
        check_frame(stack[0], "main", 25)
        found = variables(dap, stack[0])
        expect(value(found, "count") == "42", f"count = {value(found, 'count')}")
        expect(value(found, "a") == "4.5", f"a = {value(found, 'a')}")
        point = fields(dap, found["pt"])
        expect(point.get("x") == "3" and point.get("ok") == "true", f"pt = {point}")
        text = fields(dap, found["name"])
        expect(text.get("len") == "4" and "ofir" in text.get("ptr", ""), f"name = {text}")

        dap.request("next", {"threadId": thread})
        thread = stopped(dap, "step")
        check_frame(frames(dap, thread)[0], "main", 26)

        dap.request("continue", {"threadId": thread})
        code = dap.event("exited").get("exitCode")
        expect(code == 0, f"the program exited with {code}")
    finally:
        dap.close()


def check_attach(adapter: dict, exe: Path, source: Path) -> None:
    # No pipe for the output: lldb-dap 18 loses an attach to a process
    # whose stdout is a pipe.
    program = subprocess.Popen([str(exe)], stdout=subprocess.DEVNULL)
    try:
        time.sleep(0.5)
        expect(program.poll() is None, "the program did not start")
        dap = Adapter(adapter["command"])
        try:
            start(dap, adapter, "attach", {"pid": program.pid},
                  source, [], ["tick"])
            body = dap.event("stopped")
            if body.get("reason") != "breakpoint":
                # gdb stops the process when it attaches; lldb-dap resumes it.
                dap.request("continue", {"threadId": body.get("threadId", 1)})
                body = dap.event("stopped")
            expect(body.get("reason") == "breakpoint", f"stopped for {body.get('reason')}")
            thread = body.get("threadId", 1)
            stack = frames(dap, thread)
            check_frame(stack[0], "tick", 4)
            expect(any("main" in frame.get("name", "") for frame in stack[1:]),
                   "the stack has no `main` under `tick`")
            found = variables(dap, stack[0])
            round_ = int(value(found, "round"))
            expect(0 <= round_ < 3000, f"round = {round_}")
        finally:
            dap.close()
    finally:
        program.kill()
        program.wait()


def can_attach() -> bool:
    """Whether a process may attach to one that is not its child: only
    checked on Linux, where `ptrace_scope` says it."""
    if not sys.platform.startswith("linux"):
        return False
    try:
        scope = Path("/proc/sys/kernel/yama/ptrace_scope").read_text().strip()
    except OSError:
        return True
    return scope == "0" or (scope == "1" and os.geteuid() == 0)


def adapters() -> list[dict]:
    found = []
    gdb = shutil.which("gdb")
    if gdb is not None:
        probe = subprocess.run([gdb, "-nx", "-batch", "-ex", "python import gdb.dap"],
                               capture_output=True, text=True)
        if probe.returncode == 0 and not probe.stderr.strip():
            found.append({"name": "gdb", "command": [gdb, "-i=dap", "-q", "-nx"],
                          "configureFirst": True})
    for name in ["lldb-dap", "lldb-vscode"] + [f"lldb-dap-{v}" for v in range(30, 16, -1)]:
        path = shutil.which(name)
        if path is not None:
            found.append({"name": name, "command": [path], "configureFirst": False})
            break
    return found


def build(compiler: str, source: Path, exe: Path) -> None:
    cmd = [compiler, "build", str(source), "-o", str(exe), "-g"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise Failure(f"{' '.join(cmd)}\n{result.stdout}{result.stderr}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()
    if sys.platform == "win32":
        print("dap: skipped on Windows (ADR 0072)")
        return 0
    found = adapters()
    if not found:
        print("dap: skipped, neither gdb 14+ nor lldb-dap is installed")
        return 0
    compiler = str(Path(args.compiler).resolve())
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        main_dir = Path(tmp) / "launch"
        wait_dir = Path(tmp) / "attach"
        main_dir.mkdir()
        wait_dir.mkdir()
        (main_dir / "main.of").write_text(PROGRAM)
        (wait_dir / "main.of").write_text(WAITING)
        try:
            build(compiler, main_dir / "main.of", main_dir / "main")
            build(compiler, wait_dir / "main.of", wait_dir / "main")
        except Failure as error:
            print(f"FAIL: {error}")
            return 1
        checks = [("launch", check_launch, main_dir)]
        if can_attach():
            checks.append(("attach", check_attach, wait_dir))
        else:
            print("dap: attach skipped, not Linux or ptrace_scope does not allow it")
        for adapter in found:
            for what, check, directory in checks:
                try:
                    check(adapter, directory / "main", directory / "main.of")
                    print(f"ok: {what} with {adapter['name']}")
                except Failure as error:
                    print(f"FAIL: {what} with {adapter['name']}: {error}")
                    ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
