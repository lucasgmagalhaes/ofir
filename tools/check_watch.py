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


# Imports, attributes, comments and quotes in entries; `:show`, `:reset`,
# and an entry that the input ends inside.
SESSION_MORE = """\
import std.strings

@cold fn three() int {   // a comment with a { brace
    return 3
}
println("a \\" ( {three()}")
:show
:reset
three()
fn open() {
"""


def check_repl_more(compiler):
    result = subprocess.run([compiler, "repl"], input=SESSION_MORE, capture_output=True,
                            text=True, timeout=600)
    if result.returncode != 0:
        fail(f"repl exited with {result.returncode}: {result.stderr[-2000:]}")
    for want in ['a " ( 3\n', "1 | import std.strings\n", "| @cold fn three() int {"]:
        if want not in result.stdout:
            fail(f"repl printed {result.stdout!r}, without {want!r}\n"
                 f"{result.stderr[-2000:]}")
    if "three" not in result.stderr:
        fail(f"`:reset` kept `three`: {result.stderr[-2000:]}")
    print("watch: repl session ok")


def check_repl_env(compiler, tmp):
    blocker = tmp / "a-file"
    blocker.write_text("")
    env = dict(os.environ, TMPDIR=str(blocker))
    result = subprocess.run([compiler, "repl"], input="", capture_output=True, text=True,
                            env=env, timeout=600)
    if result.returncode != 1 or "cannot create" not in result.stderr:
        fail(f"repl under a file: {result.returncode} {result.stderr[-2000:]}")
    # Without TMPDIR or TEMP, under /tmp; output that is not the earlier
    # output and more is shown whole.
    env = {k: v for k, v in os.environ.items() if k not in ("TMPDIR", "TEMP")}
    session = 'import std.time\nprintln("{time.unixNanos()}")\nprintln("x")\n'
    result = subprocess.run([compiler, "repl"], input=session, capture_output=True,
                            text=True, env=env, timeout=600)
    if result.returncode != 0 or result.stdout.count("\n") < 3:
        fail(f"repl without TMPDIR: {result.returncode} {result.stdout!r}"
             f"{result.stderr[-2000:]}")
    print("watch: repl directories ok")


def check_repl_terminal(compiler):
    if os.name == "nt":
        return
    import pty
    master, slave = pty.openpty()
    proc = subprocess.Popen([compiler, "repl"], stdin=slave, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    os.close(slave)
    os.write(master, b"println(5)\n")
    time.sleep(1)
    os.close(master)   # the terminal goes away: the read fails
    out, err = proc.communicate(timeout=600)
    if proc.returncode != 0 or not out.startswith("> "):
        fail(f"repl on a terminal: {proc.returncode} {out!r} {err[-2000:]}")
    print("watch: repl prompt ok")


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


def check_watch_roots(compiler, work):
    # A file without a directory, then no file: both watch the current
    # directory, past hidden files, skipped directories and a deep tree.
    work.mkdir()
    (work / "main.of").write_text('fn main() {\n    println("one")\n}\n')
    (work / ".hidden.of").write_text("")
    (work / "build").mkdir()
    deep = work.joinpath(*["d"] * 34)
    deep.mkdir(parents=True)
    (deep / "x.of").write_text("")
    env = dict(os.environ, OFIR_WATCH_RUNS="1")
    for args in [["run", "main.of"], ["check"]]:
        result = subprocess.run([compiler, *args, "--watch"], cwd=work, env=env,
                                capture_output=True, text=True, timeout=600)
        if "[watch] finished" not in result.stderr:
            fail(f"watch {args}: {result.returncode} {result.stderr[-2000:]}")
    # A limit that is not a number is no limit: the watch waits.
    env = dict(os.environ, OFIR_WATCH_RUNS="x")
    child = subprocess.Popen([compiler, "run", "main.of", "--watch"], cwd=work, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    line = ""
    while "[watch] waiting" not in line:
        line = child.stderr.readline()
        if line == "":
            fail("a watch without a limit stopped")
    child.kill()
    child.communicate()
    print("watch: roots ok")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    check_repl(compiler)
    check_repl_more(compiler)
    check_repl_terminal(compiler)
    with tempfile.TemporaryDirectory() as tmp:
        check_repl_env(compiler, Path(tmp))
        check_watch(compiler, Path(tmp))
        check_watch_roots(compiler, Path(tmp) / "roots")
    return 0


if __name__ == "__main__":
    sys.exit(main())
