#!/usr/bin/env python3
"""Runs the programs that start tasks or share state between them
(`import std.task` or `import std.sync`) with ThreadSanitizer (OFIR-74,
ADR 0087).

The generated code is instrumented by clang (`-fsanitize=thread`) and the
C runtime is built with it too; gcc links them, as in check_asan.py. A run
program must print its `.out` file, a panic program must exit with status
101, and no program may trigger a data race report. Without gcc's
ThreadSanitizer runtime the check reports itself unavailable: skipped, or
an error under `--require`.

    python3 tools/check_tsan.py [--compiler build/stage0/ofir] [--require]
"""

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_asan import build, build_runtime  # noqa: E402
from check_leaks import programs  # noqa: E402
from safety import unavailable  # noqa: E402

SANITIZE = ["-fsanitize=thread", "-fno-omit-frame-pointer"]
CONCURRENT = re.compile(r"^import std\.(task|sync)\b", re.MULTILINE)


def concurrent_programs() -> list:
    panics = sorted((ROOT / "tests" / "panic").glob("*.of"))
    return [p for p in programs() + panics if CONCURRENT.search(p.read_text(errors="replace"))]


def check(compiler: str, path: Path, runtime: Path, out: Path):
    exe = build(compiler, path, runtime, out, SANITIZE, "-fsanitize=thread")
    if exe is None:
        return None
    env = dict(os.environ, TSAN_OPTIONS="halt_on_error=1:exitcode=66:second_deadlock_stack=1")
    run = subprocess.run([str(exe)], capture_output=True, text=True, env=env,
                         stdin=subprocess.DEVNULL, timeout=300)
    name = path.relative_to(ROOT)
    if "ThreadSanitizer" in run.stderr:
        report = [l for l in run.stderr.splitlines() if "WARNING" in l or "ERROR" in l]
        return f"{name}: {(report or ['ThreadSanitizer report'])[0].strip()}"
    if "panic" in path.parts:
        return None if run.returncode == 101 else f"{name}: exit status {run.returncode}"
    expected = path.with_suffix(".out")
    if run.returncode != 0:
        return f"{name}: exit status {run.returncode}"
    if expected.exists() and run.stdout != expected.read_text():
        return f"{name}: the output differs from {expected.name}"
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", default=str(ROOT / "build" / "stage0" / "ofir"))
    parser.add_argument("--require", action="store_true",
                        help="fail instead of skipping when ThreadSanitizer is unavailable")
    args = parser.parse_args()
    reason = unavailable("tsan")
    if reason:
        print(f"tsan: unavailable ({reason}), {'failed' if args.require else 'skipped'}")
        return 1 if args.require else 0
    paths = concurrent_programs()
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        runtime = build_runtime(out, SANITIZE, "tsan")
        with concurrent.futures.ThreadPoolExecutor() as pool:
            results = list(pool.map(lambda p: check(args.compiler, p, runtime, out), paths))
    problems = [r for r in results if r]
    for problem in problems:
        print(f"error: {problem}")
    print(f"tsan: {len(results)} programs, {len(problems)} with data races or wrong results")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
