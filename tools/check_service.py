#!/usr/bin/env python3
"""Checks the compiler service contract of stage 1 (roadmap 6.25g,
ADR 0053, docs/design/toolchain.md "Compiler service").

Checks the exit statuses (0 success, 1 errors or failed tests, 2 usage,
101 an internal compiler error or a panicking program, the program's own
status under `run`), that `ofir check` stops before code generation, that
`ofir metadata` prints the documented JSON, and that a build interrupted
by SIGINT at any point leaves a cache from which the next build succeeds.

    python3 tools/check_service.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "tests" / "run" / "modules" / "app" / "main.of"
APP_OUT = (ROOT / "tests" / "run" / "modules" / "app" / "main.out").read_text()

GOOD = 'fn main() {\n    print("hi")\n}\n'
BAD = "fn main() {\n    const x: int = true\n}\n"
EXIT3 = "import std.os\n\nfn main() {\n    os.exit(3)\n}\n"
PANIC = 'fn main() {\n    panic("boom")\n}\n'
FAILING_TEST = "@test\nfn wrong() {\n    assert(1 == 2)\n}\n"


class Checker:
    def __init__(self, compiler: str, tmp: Path, env: dict[str, str]) -> None:
        self.compiler = compiler
        self.tmp = tmp
        self.env = env
        self.ok = True

    def write(self, name: str, text: str) -> str:
        path = self.tmp / name
        path.write_text(text)
        return str(path)

    def run(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([self.compiler, *args], capture_output=True, text=True,
                              env=self.env, cwd=self.tmp, timeout=300)

    def expect(self, condition: bool, what: str, detail: str = "") -> None:
        if condition:
            print(f"ok: {what}")
        else:
            print(f"FAIL: {what}\n{detail}")
            self.ok = False

    def status(self, expected: int, what: str, *args: str) -> subprocess.CompletedProcess[str]:
        result = self.run(*args)
        self.expect(result.returncode == expected,
                    f"{what}: exit {expected}",
                    f"got {result.returncode}\n{result.stdout}{result.stderr}")
        return result


def exit_statuses(c: Checker) -> None:
    good, bad = c.write("good.of", GOOD), c.write("bad.of", BAD)
    c.status(0, "version", "version")
    c.status(2, "no command")
    c.status(2, "unknown command", "frobnicate", good)
    c.status(2, "unknown option", "build", good, "--frobnicate")
    c.status(2, "no input", "check")
    c.status(2, "explain of a non-code", "explain", "nonsense")
    c.status(1, "unreadable input", "check", str(c.tmp / "missing.of"))
    c.status(0, "check of a correct program", "check", good)
    result = c.status(1, "check of a wrong program", "check", bad, "--message-format=json")
    lines = [json.loads(line) for line in result.stderr.splitlines() if line]
    c.expect(len(lines) > 0 and all(d["severity"] in ("error", "warning") for d in lines),
             "check: JSON diagnostics on stderr", result.stderr)
    c.status(1, "build of a wrong program", "build", bad, "-o", str(c.tmp / "bad"))
    c.status(3, "run passes the program's status", "run", c.write("exit3.of", EXIT3))
    c.status(101, "run of a panicking program", "run", c.write("panic.of", PANIC))
    c.status(1, "a failing test", "test", c.write("failing_test.of", FAILING_TEST))


def check_builds_nothing(c: Checker) -> None:
    good = c.write("only_check.of", GOOD)
    c.status(0, "check", "check", good, "-o", str(c.tmp / "only_check"))
    c.expect(not (c.tmp / "only_check").exists() and not (c.tmp / "only_check.ll").exists(),
             "check writes no output")


def metadata(c: Checker) -> None:
    result = c.status(0, "metadata", "metadata", str(APP))
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError:
        c.expect(False, "metadata prints one JSON object", result.stdout)
        return
    names = [m["name"] for m in data.get("modules", [])]
    c.expect(data.get("format") == 1 and isinstance(data.get("compiler"), str),
             "metadata: format and compiler version", result.stdout)
    c.expect(data.get("root") == names[0] and Path(data["modules"][0]["path"]) == APP,
             "metadata: the root module first, with its path", result.stdout)
    c.expect("std.core" in names and len(names) > 2 and all(
        m["std"] == m["name"].startswith("std.") for m in data["modules"]),
        "metadata: the imported and std modules", result.stdout)
    c.expect(all(Path(m["path"]).is_file() for m in data["modules"]),
             "metadata: every path is a file", result.stdout)
    c.status(1, "metadata of a file that does not parse", "metadata",
             c.write("unparsed.of", "fn main( {\n}\n"))


def interrupt(child: subprocess.Popen[bytes]) -> None:
    """SIGINT to the build's process group; Windows has neither, so the build is killed."""
    if hasattr(os, "killpg"):
        os.killpg(child.pid, signal.SIGINT)
    else:
        child.kill()


def interrupted_builds(c: Checker) -> None:
    exe = c.tmp / "app"
    for delay in (0.05, 0.15, 0.3, 0.6, 1.0):
        cmd = [c.compiler, "build", str(APP), "-o", str(exe), "-j", "2"]
        child = subprocess.Popen(cmd, env=c.env, cwd=c.tmp, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(delay)
        if child.poll() is None:
            interrupt(child)
        child.wait(timeout=300)
        rebuilt = c.run("build", str(APP), "-o", str(exe))
        ran = subprocess.run([str(exe)], capture_output=True, text=True) \
            if rebuilt.returncode == 0 else None
        c.expect(ran is not None and ran.stdout == APP_OUT,
                 f"a build interrupted after {delay}s, then rebuilt",
                 rebuilt.stdout + rebuilt.stderr)
    objects = list((Path(c.env["OFIR_HOME"]) / "cache" / "objects").glob("*/*.o"))
    # ELF, Mach-O, PE, and COFF for x86-64 and AArch64.
    magic = (b"\x7fELF", b"\xcf\xfa\xed\xfe", b"MZ", b"\x64\x86", b"\x64\xaa")
    c.expect(len(objects) > 0 and all(p.read_bytes()[:4].startswith(magic) for p in objects),
             "every cached object is a whole object file")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, OFIR_HOME=str(Path(tmp) / "home"))
        work = Path(tmp) / "work"
        work.mkdir()
        c = Checker(compiler, work, env)
        exit_statuses(c)
        check_builds_nothing(c)
        metadata(c)
        interrupted_builds(c)
        return 0 if c.ok else 1


if __name__ == "__main__":
    sys.exit(main())
