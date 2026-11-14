#!/usr/bin/env python3
"""Checks that a debugger can step through a program built with `-g`
(roadmap 6.25f, docs/design/toolchain.md).

Builds a small program with `-g`, and again with `--release -g`, and
runs gdb on each in batch mode: a breakpoint on a function and on a line
stops there, the parameters and locals print with their values, a struct
prints its fields through a reference, a `str` prints its text, a
generic instance has its parameters, and the backtrace names the
callers. Skipped when gdb is not installed.

    python3 tools/check_debugger.py --compiler build/stage1/ofir
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

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

# gdb commands, and what their output must contain, for each build.
DEBUG = (
    ["break area", "break twice", "break main.of:25", "run",
     "info args", "print *p", "print w", "continue",
     "info args", "bt", "continue",
     "info locals", "print name"],
    ["Breakpoint 1, area (p=0x", "scale = 1.5", "$1 = {x = 3, y = -4, ok = true}",
     "Breakpoint 2, twice (value=42, n=5)", "in main () at", "count = 42",
     "pt = {x = 3, y = -4, ok = true}", "a = 4.5", 'ptr = 0x', '"ofir', "len = 4"],
)
RELEASE = (
    ["break main.of:24", "run", "info locals", "bt"],
    ["main () at", "count = 42", "a = 4.5", "pt = {x = 3, y = -4"],
)


def check(gdb: str, exe: Path, commands: list[str], expected: list[str], what: str) -> bool:
    # A script, not `-ex` arguments: a gdb shim on Windows splits arguments at spaces.
    script = exe.parent / f"{exe.stem}.gdb"
    script.write_text("".join(f"{command}\n" for command in commands))
    args = [gdb, "-batch", "-nx", "-x", str(script), str(exe)]
    result = subprocess.run(args, capture_output=True, text=True, timeout=120)
    missing = [text for text in expected if text not in result.stdout]
    if missing:
        print(f"FAIL: {what}: gdb printed no {missing[0]!r}")
        print(result.stdout + result.stderr)
        return False
    print(f"ok: {what}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()
    gdb = shutil.which("gdb")
    if gdb is None:
        print("debugger: skipped, gdb is not installed")
        return 0
    compiler = str(Path(args.compiler).resolve())
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "main.of"
        source.write_text(PROGRAM)
        builds = [("debug", ["-g"], DEBUG), ("release", ["--release", "-g"], RELEASE)]
        for name, flags, (commands, expected) in builds:
            exe = Path(tmp) / name
            cmd = [compiler, "build", str(source), "-o", str(exe), *flags]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                print(f"FAIL: {' '.join(cmd)}\n{result.stdout}{result.stderr}")
                return 1
            ok = check(gdb, exe, commands, expected, f"gdb on a {name} build") and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
