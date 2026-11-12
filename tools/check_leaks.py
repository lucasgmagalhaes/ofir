#!/usr/bin/env python3
"""Runs every program of tests/run and examples/ under valgrind (roadmap 3.2).

A program must free everything it allocates and make no invalid memory
access: drops must run exactly once. Requires valgrind.

    python3 tools/check_leaks.py [--compiler build/stage0/ofir]
"""

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def programs() -> list:
    found = []
    for path in sorted((ROOT / "tests" / "run").rglob("*.of")):
        project = next((p for p in path.parents if (p / "main.of").exists()
                        and p != ROOT / "tests" / "run"), None)
        if project is None or path == project / "main.of":
            found.append(path)
    found += sorted((ROOT / "examples").glob("*.of"))
    return found


# Every block from malloc: valgrind does not see a leaked block of the pool.
SYSTEM_ALLOC = dict(os.environ, OFIR_SYSTEM_ALLOC="1")


def check(compiler: str, path: Path, out_dir: Path):
    exe = out_dir / (path.stem + str(abs(hash(path))))
    build = subprocess.run([compiler, "build", str(path), "-o", str(exe)],
                           capture_output=True, text=True)
    if build.returncode != 0:
        return None   # not a program stage 0 compiles (examples of later features)
    run = subprocess.run(["valgrind", "--leak-check=full", "--error-exitcode=9",
                          "--errors-for-leak-kinds=definite,indirect", str(exe)],
                         capture_output=True, text=True, stdin=subprocess.DEVNULL,
                         env=SYSTEM_ALLOC)
    if run.returncode != 9:
        return None
    lines = [l for l in run.stderr.splitlines()
             if "lost" in l or "Invalid" in l or "uninitialised" in l]
    return f"{path.relative_to(ROOT)}: " + "; ".join(l.split("== ", 1)[-1] for l in lines[:3])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", default=str(ROOT / "build" / "stage0" / "ofir"))
    args = parser.parse_args()
    if not shutil.which("valgrind"):
        print("leaks: valgrind is not installed, skipped")
        return 0
    with tempfile.TemporaryDirectory() as out:
        with concurrent.futures.ThreadPoolExecutor() as pool:
            results = list(pool.map(lambda p: check(args.compiler, p, Path(out)), programs()))
    problems = [r for r in results if r]
    for problem in problems:
        print(f"error: {problem}")
    print(f"leaks: {len(results)} programs, {len(problems)} with leaks or memory errors")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
