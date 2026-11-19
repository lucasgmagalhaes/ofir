#!/usr/bin/env python3
"""Builds and runs the representative applications of apps/ (OFIR-76,
ADR 0090).

Each app is a directory with an `app.json`:

    {
      "root": "main.of",            // the Ofir source to build
      "lib": "static",              // optional: build a C library and its header
      "consumer": "main.c",         // with "lib": the C program that links it
      "cases": [
        {"name": "search", "args": ["todo", "testdata"], "status": 0}
      ]
    }

A case runs the program (or the consumer) in the app's directory with its
arguments; its standard output must equal `expected/<name>.out` and its
exit status `status` (default 0). Every app is built twice, in dev and
with `--release`, and both builds must pass every case, so a change to the
compiler, std or the runtime that breaks a real program fails here.

    python3 tools/check_apps.py --compiler build/stage1/ofir [app ...]
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APPS = ROOT / "apps"
sys.path.insert(0, str(ROOT / "tests" / "runner"))
from run_tests import c_tools  # noqa: E402

EXE = ".exe" if os.name == "nt" else ""


def run(args: list, cwd: Path = ROOT) -> subprocess.CompletedProcess:
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True,
                          stdin=subprocess.DEVNULL, timeout=600)


def describe(proc: subprocess.CompletedProcess) -> str:
    return (proc.stdout + proc.stderr).strip()[-2000:]


def build(compiler: str, app: Path, spec: dict, out: Path, release: bool) -> tuple:
    """Builds the app into `out`; returns the program to run, or an error."""
    flags = ["--deny-warnings"] + (["--release"] if release else [])
    root = app / spec["root"]
    if "lib" not in spec:
        exe = out / (app.name + EXE)
        proc = run([compiler, "build", str(root), "-o", str(exe), *flags])
        return (exe, None) if proc.returncode == 0 else (None, describe(proc))
    lib = out / f"lib{root.stem}.a"
    proc = run([compiler, "build", str(root), "-o", str(lib), f"--lib={spec['lib']}", *flags])
    if proc.returncode != 0:
        return None, describe(proc)
    clang, _ = c_tools()
    runtime = os.environ.get("OFIR_RUNTIME") or str(ROOT / "build" / "libofir_rt.a")
    exe = out / ("consumer" + EXE)
    args = [clang, "-std=c11", "-Wall", "-Werror", "-I", str(out), str(app / spec["consumer"]),
            str(lib), runtime, "-lm", "-lpthread", "-o", str(exe)]
    if os.name == "nt":   # the MinGW libraries, as the compiler links them
        args.insert(1, "--target=x86_64-w64-windows-gnu")
    proc = run(args)
    return (exe, None) if proc.returncode == 0 else (None, f"cannot build the consumer\n"
                                                         + describe(proc))


def check_app(compiler: str, app: Path, tmp: Path) -> list:
    spec = json.loads((app / "app.json").read_text())
    failures = []
    for release in (False, True):
        mode = "release" if release else "dev"
        out = tmp / app.name / mode
        out.mkdir(parents=True, exist_ok=True)
        exe, error = build(compiler, app, spec, out, release)
        if exe is None:
            failures.append(f"apps/{app.name} ({mode}): build failed\n{error}")
            continue
        for case in spec["cases"]:
            proc = run([str(exe), *case["args"]], cwd=app)
            label = f"apps/{app.name} {case['name']} ({mode})"
            status = case.get("status", 0)
            expected = (app / "expected" / f"{case['name']}.out").read_text()
            if proc.returncode != status:
                failures.append(f"{label}: exit status {proc.returncode}, not {status}\n"
                                + describe(proc))
            elif proc.stdout != expected:
                failures.append(f"{label}: the output differs from expected/{case['name']}.out\n"
                                + proc.stdout[-2000:])
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", default=str(ROOT / "build" / "stage1" / ("ofir" + EXE)))
    parser.add_argument("apps", nargs="*", help="the apps to check (default: all)")
    args = parser.parse_args()
    apps = sorted(p.parent for p in APPS.glob("*/app.json"))
    if args.apps:
        apps = [a for a in apps if a.name in args.apps]
    failures = []
    cases = 0
    with tempfile.TemporaryDirectory(prefix="ofir-apps-") as tmp:
        for app in apps:
            failures += check_app(args.compiler, app, Path(tmp))
            cases += 2 * len(json.loads((app / "app.json").read_text())["cases"])
    for failure in failures:
        print(failure)
    print(f"apps: {len(apps)} apps, {cases} cases, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
