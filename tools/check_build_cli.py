#!/usr/bin/env python3
"""Checks the failures of `ofir build`, `run`, `test`, `bench` and
`explain` that the test suites cannot reach (docs/design/toolchain.md):

- files the build cannot write: the `.ll` of one unit and of several, and
  the C header of a library; JSON messages for them;
- a static library with `build.native` files (E0918);
- a run for another target goes through OFIR_RUNNER (ADR 0066), here with a
  stand-in clang, and a runner that cannot start gives E0902 for `run`,
  `test` and `bench`;
- a record of a build whose clang does not run and whose runtime is
  missing;
- a run with no `TMPDIR` nor `TEMP`;
- a benchmark that fails;
- `explain` with no code, a malformed code and with no explanation;
  `help`.

    python3 tools/check_build_cli.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MAIN = "fn main() {\n    print(\"hi\")\n}\n"
LIBRARY = "pub fn one() int {\n    return 1\n}\n"
FAILING_BENCH = ("import std.testing.Bencher\n\n@bench\nfn fails(b: mut Bencher) {\n"
                 "    panic(\"no\")\n}\n\nfn main() {\n}\n")
TEST = "import std.testing.assert\n\n@test\nfn works() {\n    assert(true)\n}\n"
NATIVE = ('{\n  "name": "native",\n  "version": "0.1.0",\n'
          '  "build": {"kind": "lib", "root": "lib.of", "native": ["x.c"]},\n}\n')
# Stands in for clang: it writes each `-o` file, empty.
FAKE_CLANG = '#!/bin/sh\nwhile [ $# -gt 0 ]; do\n  [ "$1" = "-o" ] && : > "$2"\n  shift\ndone\n'
CROSS = "aarch64-linux-gnu"


def fail(message: str) -> None:
    print(f"build-cli: {message}", file=sys.stderr)
    sys.exit(1)


def ofir(compiler: str, cwd: Path, *args: str, status: int, env=None) -> str:
    result = subprocess.run([compiler, *args], cwd=cwd, capture_output=True, text=True,
                            errors="replace", timeout=300, env=env)
    if result.returncode != status:
        fail(f"{' '.join(args)} exited with {result.returncode}, not {status}:\n"
             f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result.stdout + result.stderr


def expect(output: str, text: str, what: str) -> None:
    if text not in output:
        fail(f"{what}: {text!r} not in {output[-2000:]!r}")


def writes(compiler: str, work: Path) -> None:
    (work / "main.of").write_text(MAIN)
    expect(ofir(compiler, work, "build", "main.of", "--emit=llvm", "-o", "nope/out", status=1),
           "cannot write `nope/out.ll`", "one unit")
    expect(ofir(compiler, work, "build", "main.of", "-o", "nope/out", status=1),
           "cannot write `nope/out.0.ll`", "several units")
    expect(ofir(compiler, work, "build", "main.of", "-o", "nope/out", "--message-format=json",
                status=1), '"code":"E0902"', "a JSON message")
    library = work / "library"
    (library / "lib.h").mkdir(parents=True)
    (library / "lib.of").write_text(LIBRARY)
    expect(ofir(compiler, library, "build", "lib.of", "--lib=shared", "-o", "out", status=1),
           "cannot write `lib.h`", "a header")
    native = work / "native"
    native.mkdir()
    (native / "ofir.json").write_text(NATIVE)
    (native / "lib.of").write_text(LIBRARY)
    (native / "x.c").write_text("int x(void) { return 1; }\n")
    expect(ofir(compiler, native, "build", "lib.of", "--lib=static", status=1),
           "E0918", "a static library with native files")


def runners(compiler: str, work: Path) -> None:
    runtime = work / "rt"
    (runtime / CROSS).mkdir(parents=True)
    (runtime / "libofir_rt.a").write_text("")
    (runtime / CROSS / "libofir_rt.a").write_text("")
    clang = work / "fakeclang"
    clang.write_text(FAKE_CLANG)
    clang.chmod(0o755)
    (work / "main.of").write_text(MAIN)
    (work / "unit_test.of").write_text(TEST)
    (work / "bench.of").write_text(FAILING_BENCH)
    env = dict(os.environ, OFIR_RUNTIME=str(runtime / "libofir_rt.a"), OFIR_CLANG=str(clang),
               OFIR_RUNNER="echo ran")
    expect(ofir(compiler, work, "run", "main.of", f"--target={CROSS}", status=0, env=env),
           "ran ", "a run through OFIR_RUNNER")
    env["OFIR_RUNNER"] = str(work / "no-runner")
    for command, file in [("run", "main.of"), ("test", "unit_test.of"), ("bench", "bench.of")]:
        expect(ofir(compiler, work, command, file, f"--target={CROSS}", status=1, env=env),
               "error[E0902]: cannot run", f"{command} with a runner that cannot start")


def records(compiler: str, work: Path) -> None:
    (work / "main.of").write_text(MAIN)
    env = dict(os.environ, OFIR_CLANG=str(work / "no-clang"), OFIR_RUNTIME=str(work / "no-rt"))
    ofir(compiler, work, "build", "main.of", "--emit=llvm", "--record=r.json", status=0, env=env)
    record = json.loads((work / "r.json").read_text())
    if record["clang"] != "" or record["runtime"] != "":
        fail(f"a clang that does not run or a missing runtime is recorded: {record}")
    alone = work / "bin" / "ofir"   # a copy with no runtime beside it
    alone.parent.mkdir()
    shutil.copy(compiler, alone)
    env = {k: v for k, v in env.items() if k != "OFIR_RUNTIME"}
    ofir(str(alone), work, "build", "main.of", "--emit=llvm", "--record=r.json", status=0,
         env=env)
    if json.loads((work / "r.json").read_text())["runtime"] != "":
        fail("a runtime that is not found is recorded")


def temps(compiler: str, work: Path) -> None:
    (work / "main.of").write_text(MAIN)
    env = {k: v for k, v in os.environ.items() if k not in ("TMPDIR", "TEMP")}
    expect(ofir(compiler, work, "run", "main.of", status=0, env=env), "hi", "a run in /tmp")


def benches(compiler: str, work: Path) -> None:
    (work / "bench.of").write_text(FAILING_BENCH)
    expect(ofir(compiler, work, "bench", "bench.of", status=1), "FAILED (exit status",
           "a failing benchmark")


def explains(compiler: str, work: Path) -> None:
    expect(ofir(compiler, work, "explain", status=2), "needs one code", "no code")
    expect(ofir(compiler, work, "help", status=0), "usage: ofir", "help")
    expect(ofir(compiler, work, "explain", "E12x4", status=2), "is not an error code",
           "a malformed code")
    env = dict(os.environ, OFIR_ERRORS=str(work))
    expect(ofir(compiler, work, "explain", "E9999", status=1, env=env), "no explanation",
           "a code with no explanation")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    checks = (writes, records, temps, benches, explains)
    for check in checks + ((runners,) if os.name != "nt" else ()):
        with tempfile.TemporaryDirectory() as tmp:
            check(compiler, Path(tmp))
    print("build-cli: ok")


if __name__ == "__main__":
    main()
