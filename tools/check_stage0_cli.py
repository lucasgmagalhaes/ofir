#!/usr/bin/env python3
"""Checks the command line of stage 0 (docs/bootstrap.md) and its driver
errors (ADR 0103, roadmap 6.33d).

- the commands `version` and `help`, no command and an unknown one;
- an unknown option, a missing input and a second input;
- an input that does not exist or is a directory, a module path that is a
  directory, an import path too long for a module, an empty `_test.of`
  and a project file named `.of`;
- an `OFIR_STD` that does not exist, for a program that imports nothing,
  and the slice and optional types it then rejects;
- the options `--stop-after=lex|parse|check`, `--message-format=human` and
  `--time-passes`;
- a build without `-o`, named after the input in the current directory;
- an installed layout: the runtime and `std` found next to the compiler
  with `OFIR_RUNTIME` and `OFIR_STD` unset, and the error when the
  runtime is not there;
- the E0902 errors of the back end: clang missing or failing, an output
  that cannot be written, and no temporary directory for `run`.

    python3 tools/check_stage0_cli.py --compiler build/stage0/ofir --runtime build/libofir_rt.a
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

NO_STD = """type Count = int

fn f(_a: &[int], _b: int?, _c: Count, _d: Count) {
}

fn main() {
    const _l = [1, 2]
    const _r = [0; 3]
}
"""

PROGRAM = """fn main() {
    const small = 1.0e-5
    println("cli {small > 0.0}")
}
"""


class Checker:
    def __init__(self, compiler, env):
        self.compiler = compiler
        self.env = env
        self.failures = 0
        self.count = 0

    def expect(self, name, args, code, text=None, env=None, cwd=None, stream="stderr"):
        self.count += 1
        result = subprocess.run([str(self.compiler), *args], capture_output=True, text=True,
                                env=env or self.env, cwd=cwd, timeout=120)
        out = result.stdout if stream == "stdout" else result.stderr
        if result.returncode != code or (text is not None and text not in out):
            self.failures += 1
            print(f"FAIL {name}: exit {result.returncode} (expected {code})")
            print(f"  wanted {text!r} in {stream}:\n{out}")
        else:
            print(f"ok   {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / "stage0" / "ofir")
    parser.add_argument("--runtime", type=Path, default=ROOT / "build" / "libofir_rt.a")
    opts = parser.parse_args()
    compiler = opts.compiler.resolve()
    env = dict(os.environ, OFIR_STD=str(ROOT / "std"), OFIR_RUNTIME=str(opts.runtime.resolve()))
    c = Checker(compiler, env)
    with tempfile.TemporaryDirectory(prefix="ofir-cli-") as tmp:
        tmp = Path(tmp)
        source = tmp / "hello.of"
        source.write_text(PROGRAM)
        src = str(source)

        c.expect("no command", [], 2, "usage: ofir")
        c.expect("version", ["version"], 0, "(stage 0)", stream="stdout")
        c.expect("help", ["help"], 0, "usage: ofir", stream="stdout")
        c.expect("unknown command", ["frob"], 2, "unknown command `frob`")
        c.expect("unknown option", ["build", "--frob", src], 2, "unknown option `--frob`")
        c.expect("no input", ["build"], 2, "`build` needs an input file")
        c.expect("two inputs", ["build", src, src], 2, "unexpected argument")
        c.expect("input not found", ["build", str(tmp / "nope.of")], 1, "cannot read")
        c.expect("input is a directory", ["build", str(tmp)], 1, "cannot read")
        project = tmp / "proj"
        (project / "lib.of").mkdir(parents=True)
        (project / "ofir.json").write_text("{}\n")
        (project / "main.of").write_text("import lib\n\nfn main() {\n}\n")
        c.expect("module is a directory", ["build", str(project / "main.of")], 1,
                 "cannot find module `lib`")
        long_path = ".".join(["segment"] * 80)   # longer than any module path stage 0 builds
        (project / "long.of").write_text(f"import {long_path}\n\nfn main() {{\n}}\n")
        c.expect("import path too long", ["build", str(project / "long.of")], 1,
                 "cannot find module `segment.segment")
        (project / ".of").write_text("fn main() {\n}\n")
        c.expect("project file named .of", ["build", ".of", "-o", "dot"], 0, cwd=project)
        (tmp / "unit.of").write_text("fn main() {\n}\n")
        (tmp / "unit_test.of").write_text("")
        c.expect("empty test file", ["test", str(tmp / "unit.of")], 0, "0 tests",
                 stream="stdout")
        c.expect("no std directory", ["build", src, "--stop-after=check"], 0,
                 env=dict(env, OFIR_STD=str(tmp / "no-std")))
        no_std = tmp / "no_std.of"
        no_std.write_text(NO_STD)
        for name, text in (("slice", "slices without the standard library"),
                           ("option", "optional types (`T?`) without std"),
                           ("list", "list literals without the standard library")):
            c.expect(f"no std, {name}", ["build", str(no_std), "--stop-after=check"], 1, text,
                     env=dict(env, OFIR_STD=str(tmp / "no-std")))
        for stage in ("lex", "parse", "check"):
            c.expect(f"--stop-after={stage}", ["build", src, f"--stop-after={stage}",
                                              "--message-format=human"], 0)
        c.expect("--time-passes", ["build", src, "--time-passes", "-o", str(tmp / "timed")], 0,
                 "llvm")
        c.expect("float literal in the AST", ["build", src, "--emit=ast"], 0, "1e-05",
                 stream="stdout")
        c.expect("output named after the input", ["build", src], 0, cwd=tmp)
        if not (tmp / "hello").exists() and not (tmp / "hello.exe").exists():
            c.failures += 1
            print("FAIL output named after the input: no `hello` in the current directory")

        # The installed layout of docs/design/toolchain.md, section 4.
        install = tmp / "prefix"
        (install / "bin").mkdir(parents=True)
        (install / "lib" / "ofir").mkdir(parents=True)
        shutil.copy2(compiler, install / "bin" / "ofir")
        bare = {k: v for k, v in env.items() if k not in ("OFIR_STD", "OFIR_RUNTIME")}
        installed = Checker(install / "bin" / "ofir", bare)
        installed.expect("installed, no runtime", ["build", src, "-o", str(tmp / "a")], 1,
                         "cannot find the runtime library")
        shutil.copy2(opts.runtime, install / "lib" / "ofir" / "libofir_rt.a")
        os.symlink(ROOT / "std", install / "lib" / "ofir" / "std")
        installed.expect("installed", ["run", src], 0, "cli true", stream="stdout")
        c.failures += installed.failures
        c.count += installed.count

        c.expect("clang missing", ["build", src, "-o", str(tmp / "b")], 1, "cannot run",
                 env=dict(env, OFIR_CLANG=str(tmp / "no-clang")))
        c.expect("clang fails", ["build", src, "-o", str(tmp / "b")], 1, "clang failed",
                 env=dict(env, OFIR_CLANG="false"))
        c.expect("unwritable output", ["build", src, "-o", str(tmp / "none" / "x")], 1,
                 "cannot write")
        c.expect("no temporary directory", ["run", src], 1, "cannot create the output path",
                 env=dict(env, TMPDIR=str(tmp / "none"), TMP=str(tmp / "none"),
                          TEMP=str(tmp / "none")))   # Windows reads TMP and TEMP
    print(f"stage 0 cli: {c.count} checks, {c.failures} failed")
    return 1 if c.failures else 0


if __name__ == "__main__":
    sys.exit(main())
