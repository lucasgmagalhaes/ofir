#!/usr/bin/env python3
"""Checks build profiles and native inputs (ADR 0078, OFIR-66).

- `dev` is the default and `--release` is `--profile=release`; a profile
  of the manifest's `profiles` inherits one of them and sets `opt` and
  `debugInfo`, which reach clang as `-O<n>` and `-g`;
- `@cfg(debug)` holds in a profile that inherits `dev` only;
- `--release` with another `--profile` is refused, and an unknown
  profile, key or value is E0918;
- the `build.native` C files of the root and of a dependency are
  compiled with the program's flags and linked into it, and a missing
  one is E0918;
- a manifest that is not valid JSONC, or not a file, is left to the
  package checks.

The clang calls are seen through `OFIR_CLANG`, a script that logs its
arguments and runs clang.

    python3 tools/check_build_profiles.py --compiler build/stage1/ofir
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MAIN = """import mid

extern "C" {
    fn rootTwice(x: i32) i32
}

@cfg(debug)
fn mode() str {
    return "debug"
}

@cfg(!debug)
fn mode() str {
    return "release"
}

fn main() {
    const n = unsafe { rootTwice(mid.plusOne(20)) }
    println("{mode()} {n}")
}
"""

MID = """extern "C" {
    fn midPlusOne(x: i32) i32
}

pub fn plusOne(x: i32) i32 {
    return unsafe { midPlusOne(x) }
}
"""


def fail(message):
    print(f"profiles: {message}", file=sys.stderr)
    sys.exit(1)


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    clang = os.environ.get("OFIR_CLANG") or shutil.which("clang")
    if os.name == "nt" or clang is None:
        print("profiles: skipped (needs a POSIX shell and clang)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        log = tmp / "clang.log"
        wrapper = tmp / "clang.sh"
        write(wrapper, f'#!/bin/sh\necho "$@" >> "{log}"\nexec "{clang}" "$@"\n')
        wrapper.chmod(0o755)
        env = dict(os.environ, OFIR_CLANG=str(wrapper), OFIR_HOME=str(tmp / "home"))
        work = tmp / "app"
        profiles = ('{"fast": {"inherits": "release", "opt": 3},'
                    ' "check": {"opt": 1, "debugInfo": true},'
                    ' "release": {"debugInfo": true}}')
        write(work / "ofir.json", '{\n  "name": "app",\n'
              '  "build": {"kind": "exe", "root": "main.of", "native": ["c/root.c"]},\n'
              '  "dependencies": {"mid": {"path": "deps/mid"}},\n'
              f'  "profiles": {profiles}\n}}\n')
        write(work / "main.of", MAIN)
        write(work / "c" / "root.c", "int rootTwice(int x) { return 2 * x; }\n")
        mid = work / "deps" / "mid"
        write(mid / "ofir.json", '{"name": "mid", "build": {"kind": "lib", '
              '"native": ["native/mid.c"]}}\n')
        write(mid / "src" / "index.of", MID)
        write(mid / "native" / "mid.c", "int midPlusOne(int x) { return x + 1; }\n")

        def run(*flags, ok=True):
            if log.exists():
                log.unlink()
            result = subprocess.run([compiler, "run", "--no-cache", *flags, "main.of"],
                                    cwd=work, env=env, capture_output=True, text=True,
                                    timeout=600)
            if ok and result.returncode != 0:
                fail(f"run {' '.join(flags)} exited with {result.returncode}:\n"
                     f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
            calls = log.read_text() if log.exists() else ""
            return result, calls

        def expect(flags, output, level, debug_info):
            result, calls = run(*flags)
            if result.stdout.strip() != output:
                fail(f"{flags} printed {result.stdout!r}, not {output!r}")
            words = calls.split()
            if f"-O{level}" not in words:
                fail(f"{flags} did not call clang with -O{level}:\n{calls}")
            if ("-g" in words) != debug_info:
                fail(f"{flags}: -g is {'missing' if debug_info else 'given'}:\n{calls}")
            for c in ("c/root.c", "native/mid.c"):
                if c not in calls:
                    fail(f"{flags}: {c} was not compiled:\n{calls}")

        expect([], "debug 42", 0, False)
        expect(["--profile=dev"], "debug 42", 0, False)
        expect(["--profile=fast"], "release 42", 3, False)
        expect(["--profile=check"], "debug 42", 1, True)
        expect(["--release"], "release 42", 2, True)
        expect(["--profile=release"], "release 42", 2, True)

        both, _ = run("--release", "--profile=fast", ok=False)
        if both.returncode == 0 or "cannot both be given" not in both.stderr:
            fail(f"--release with --profile=fast was accepted:\n{both.stderr}")
        unknown, _ = run("--profile=nope", ok=False)
        if unknown.returncode == 0 or "E0918" not in unknown.stderr:
            fail(f"an unknown profile is not E0918:\n{unknown.stderr}")

        manifest = (work / "ofir.json").read_text()
        bad = [('"opt": 3', '"opt": 4', "`opt` of `fast`"),
               ('"inherits": "release"', '"inherits": "fast"', "`inherits` of `fast`"),
               ('"release": {"debugInfo": true}', '"release": {"inherits": "dev"}',
                "cannot have `inherits`"),
               ('"debugInfo": true}', '"debugInfo": 1}', "`debugInfo` of `check`"),
               ('"opt": 1,', '"lto": 1,', "unknown key `lto`"),
               ('"opt": 1,', '"trimPaths": 1, "opt": 1,', "`trimPaths` of `check`"),
               ('{"opt": 1, "debugInfo": true}', '1', "the profile `check` must be"),
               (profiles, "[]", "`profiles` must be an object")]
        for old, new, message in bad:
            write(work / "ofir.json", manifest.replace(old, new))
            result, _ = run(ok=False)
            if result.returncode == 0 or "E0918" not in result.stderr \
                    or message not in result.stderr:
                fail(f"{new} is not E0918 ({message}):\n{result.stderr}")
        write(work / "ofir.json", manifest.replace('"opt": 1,', '"trimPaths": true, "opt": 1,'))
        expect(["--profile=check"], "debug 42", 1, True)
        write(work / "ofir.json", manifest)

        (mid / "native" / "mid.c").unlink()
        missing, _ = run(ok=False)
        if missing.returncode == 0 or "E0918" not in missing.stderr \
                or "mid.c" not in missing.stderr:
            fail(f"a missing native file is not E0918:\n{missing.stderr}")

        # Profiles are read before the packages, from a manifest that may not be valid.
        for name, broken in [("invalid", "text"), ("folder", "dir")]:
            app = tmp / name
            write(app / "main.of", "fn main() {\n}\n")
            if broken == "dir":
                (app / "ofir.json").mkdir()
            else:
                write(app / "ofir.json", "{\n")
            result = subprocess.run([compiler, "build", "main.of"], cwd=app, env=env,
                                    capture_output=True, text=True, timeout=600)
            if (result.returncode == 0) != (broken == "dir"):
                fail(f"a manifest that is a {broken} gives {result.returncode}:\n"
                     f"{result.stderr[-2000:]}")
    print("profiles: ok")


if __name__ == "__main__":
    main()
