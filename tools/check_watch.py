#!/usr/bin/env python3
"""Checks watch mode and the REPL (OFIR-62, ADR 0074).

Watch: `ofir run main.of --watch` runs the program, and runs it again
after main.of changes (`OFIR_WATCH_RUNS=2` stops it after two runs).

REPL: a session fed on standard input keeps its items and bindings,
prints the value of an expression entry (not `()`), shows only the new
output of each entry, and drops an entry that does not compile or that
panics.

    python3 tools/check_watch.py --compiler build/stage1/ofir
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SESSION = """\
fn square(n: int) int {
    return n * n
}
let xs = [1, 2, 3]
square(4)
xs.push(4)
xs
println("hi")
missingName
panic("boom")
xs.len()
:quit
"""

EXPECTED = "16\n[1, 2, 3, 4]\nhi\n4\n"


def fail(message):
    print(f"watch: {message}", file=sys.stderr)
    sys.exit(1)


def check_repl(compiler):
    result = subprocess.run([compiler, "repl"], input=SESSION, capture_output=True,
                            text=True, timeout=600)
    if result.returncode != 0:
        fail(f"repl exited with {result.returncode}: {result.stderr[-2000:]}")
    if result.stdout != EXPECTED:
        fail(f"repl printed {result.stdout!r}, expected {EXPECTED!r}\n"
             f"{result.stderr[-2000:]}")
    for want in ["missingName", "boom"]:
        if want not in result.stderr:
            fail(f"repl did not report {want!r}: {result.stderr[-2000:]}")
    print("watch: repl ok")


def check_watch(compiler, work):
    source = work / "main.of"
    source.write_text('fn main() {\n    println("one")\n}\n')
    env = dict(os.environ, OFIR_WATCH_RUNS="2")
    child = subprocess.Popen([compiler, "run", str(source), "--watch"], cwd=work, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    first = child.stdout.readline()
    if first != "one\n":
        child.kill()
        fail(f"first run printed {first!r}: {child.stderr.read()[-2000:]}")
    time.sleep(0.5)
    source.write_text('fn main() {\n    println("two")\n}\n')
    try:
        out, err = child.communicate(timeout=300)
    except subprocess.TimeoutExpired:
        child.kill()
        fail("the change was not picked up")
    if out != "two\n" or child.returncode != 0:
        fail(f"second run printed {out!r} with {child.returncode}: {err[-2000:]}")
    if "[watch] waiting for changes" not in err:
        fail(f"no waiting message: {err[-2000:]}")
    print("watch: watch ok")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    check_repl(compiler)
    with tempfile.TemporaryDirectory() as tmp:
        check_watch(compiler, Path(tmp))
    return 0


if __name__ == "__main__":
    sys.exit(main())
