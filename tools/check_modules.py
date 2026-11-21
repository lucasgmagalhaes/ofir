#!/usr/bin/env python3
"""Checks how `ofir build` and `ofir test` load modules (spec 7.2, 12.2)
where the test suites cannot reach:

- a test file that cannot be read, does not lex or holds no item, and a
  module of `ofir test` with no test file;
- a module file that cannot be read, and `std.x` with no standard library
  found next to a copy of the compiler;
- `$OFIR_STD` that does not exist, a root file named `.of` and a package
  whose `src` does not exist.

    python3 tools/check_modules.py --compiler build/stage1/ofir
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MAIN = "fn main() {\n}\n"
LIB = "pub fn one() int {\n    return 1\n}\n"
UNREADABLE = "/proc/self/mem"   # Linux: reading it fails at the first byte


def fail(message):
    print(f"modules: {message}", file=sys.stderr)
    sys.exit(1)


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def ofir(compiler, cwd, *words, status=0, env=None):
    result = subprocess.run([compiler, *words], cwd=cwd, capture_output=True, text=True,
                            errors="replace", timeout=300, env=env)
    if result.returncode != status:
        fail(f"{' '.join(words)} in {cwd.name} exited with {result.returncode}, not "
             f"{status}:\n{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result.stdout + result.stderr


def expect(output, text, what):
    if text not in output:
        fail(f"{what}: {text!r} not in {output[-2000:]!r}")


def test_files(compiler, tmp):
    cases = {"unreadable": (None, 0), "unlexed": ('fn f() {\n    print("x)\n}\n', 1),
             "empty": ("// no tests\n", 0)}
    for name, (text, status) in cases.items():
        work = tmp / name
        write(work / "main.of", MAIN)
        if text is None:
            (work / "main_test.of").symlink_to(UNREADABLE)
        else:
            write(work / "main_test.of", text)
        ofir(compiler, work, "test", "main.of", status=status)
    work = tmp / "untested"
    write(work / "main.of", "import m\n\n" + MAIN)
    write(work / "m.of", LIB)
    expect(ofir(compiler, work, "test", "main.of"), "0 tests", "a module with no test file")


def module_files(compiler, tmp):
    work = tmp / "unread"
    write(work / "main.of", "import m\n\n" + MAIN)
    (work / "m.of").symlink_to(UNREADABLE)
    expect(ofir(compiler, work, "build", "main.of", status=1), "cannot find module `m`",
           "a module that cannot be read")
    alone = tmp / "alone"
    write(alone / "main.of", "import std.os\n\n" + MAIN)
    (alone / "bin").mkdir()
    shutil.copy(compiler, alone / "bin" / "ofir")   # no `std` next to it
    env = {k: v for k, v in os.environ.items() if k != "OFIR_STD"}
    expect(ofir(str(alone / "bin" / "ofir"), alone, "build", "main.of", status=1, env=env),
           "cannot find module `std.os`", "no standard library")


def roots(compiler, tmp):
    work = tmp / "nostd"
    write(work / "main.of", MAIN)
    ofir(compiler, work, "build", "main.of", "--stop-after=check",
         env=dict(os.environ, OFIR_STD=str(tmp / "missing")))
    work = tmp / "dotted"
    write(work / ".of", MAIN)
    write(work / "ofir.json", '{"name": "a", "build": {"kind": "exe", "root": ".of"}}\n')
    ofir(compiler, work, "build", ".of", "--emit=llvm")
    write(tmp / "d" / "lib.of", LIB)
    write(tmp / "d" / "ofir.json", '{"name": "d", "build": {"kind": "lib", "root": "lib.of"}}\n')
    app = tmp / "app"
    write(app / "main.of", 'import d\n\nfn main() {\n    print("{d.one()}")\n}\n')
    write(app / "ofir.json", '{"name": "app", "build": {"kind": "exe", "root": "main.of"}, '
          '"dependencies": {"d": {"path": "../d"}}}\n')
    ofir(compiler, app, "build", "main.of", "--emit=llvm")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    if not Path(UNREADABLE).exists():
        print("modules: skipped (needs /proc)")
        return
    for check in (test_files, module_files, roots):
        with tempfile.TemporaryDirectory() as tmp:
            check(compiler, Path(tmp))
    print("modules: ok")


if __name__ == "__main__":
    main()
