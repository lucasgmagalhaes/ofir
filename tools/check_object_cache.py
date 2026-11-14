#!/usr/bin/env python3
"""Checks the object cache of stage 1 (roadmap 6.6e, ADR 0033).

Builds a program of several modules in a fresh `$OFIR_HOME` and checks
that: the first build fills the cache; a second build reuses every
object and the program still runs; a cached object that is not an object
file is replaced; a cached object that breaks the link is recovered
from; `--no-cache` leaves the cache alone; `OFIR_CACHE_LIMIT` bounds it;
and a release build, one unit, is cached as one object.

Then it edits a copy of the program and checks that the cache key, the
unit's text, follows the dependencies: an edit that changes no code
compiles no unit, a function body compiles its own unit, and a layout
change compiles its module and the module that uses the type.

    python3 tools/check_object_cache.py --compiler build/stage1/ofir
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = ROOT / "tests" / "run" / "modules" / "app" / "main.of"
# Windows builds are one unit: MinGW's ld cannot link the thread-local
# COMDATs that units share (compiler/main.of).
ONE_UNIT = os.name == "nt"
EXPECTED = (ROOT / "tests" / "run" / "modules" / "app" / "main.out").read_text()


def objects(home: Path) -> dict[Path, bytes]:
    """The cached objects and their contents."""
    return {p: p.read_bytes() for p in sorted((home / "cache" / "objects").glob("*/*.o"))}


def build(compiler: str, env: dict[str, str], exe: Path, *flags: str) -> str:
    cmd = [compiler, "build", PROGRAM.relative_to(ROOT).as_posix(), "-o", str(exe), *flags]
    result = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"error: {' '.join(cmd)} failed:\n{result.stdout}{result.stderr}")
    output = subprocess.run([str(exe)], capture_output=True, text=True).stdout
    if output.replace("\r\n", "\n") != EXPECTED:
        sys.exit(f"error: {exe} printed:\n{output}")
    return result.stderr


def edit(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if old not in text:
        sys.exit(f"error: `{old}` is not in {path}")
    path.write_text(text.replace(old, new))


def new_objects(compiler: str, env: dict[str, str], tmp: Path, last: str) -> int:
    """Builds the copy of the program in `tmp`; the objects it added to the
    cache, after checking the last value the program printed."""
    home = Path(env["OFIR_HOME"])
    before = set(objects(home))
    exe = tmp / ("edited-app.exe" if os.name == "nt" else "edited-app")
    cmd = [compiler, "build", "app/main.of", "-o", str(exe)]
    result = subprocess.run(cmd, cwd=tmp / "edited", env=env, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"error: {' '.join(cmd)} failed:\n{result.stdout}{result.stderr}")
    lines = subprocess.run([str(exe)], capture_output=True, text=True).stdout.split()
    if not lines or lines[-1] != last:
        sys.exit(f"error: {exe} printed {lines}, expected `{last}` last")
    return len(set(objects(home)) - before)


def check_invalidation(compiler: str, env: dict[str, str], tmp: Path) -> None:
    app = tmp / "edited" / "app"
    shutil.copytree(PROGRAM.parent, app)
    new_objects(compiler, env, tmp, "1.0")
    edit(app / "geometry" / "index.of", "The module `geometry` itself", "The `geometry` module")
    check(new_objects(compiler, env, tmp, "1.0") == 0,
          "an edit that changes no code compiles no unit")
    edit(app / "geometry" / "index.of", "return 1.0", "return 1.5")
    check(new_objects(compiler, env, tmp, "1.5") == 1, "a new function body compiles its unit")
    edit(app / "geometry" / "shapes.of", "    side: float\n", "    side: float\n    tag: int\n")
    edit(app / "geometry" / "shapes.of", "Square{side}", "Square{side, tag: 0}")
    check(new_objects(compiler, env, tmp, "1.5") == (1 if ONE_UNIT else 2),
          "a new layout compiles its module and the module that uses it")


def check(ok: bool, what: str) -> None:
    if not ok:
        sys.exit(f"error: {what}")
    print(f"ok: {what}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    tmp = Path(tempfile.mkdtemp(prefix="ofir-cache-"))
    try:
        home = tmp / "home"
        env = {**os.environ, "OFIR_HOME": str(home)}
        env.pop("OFIR_CACHE_LIMIT", None)
        exe = tmp / ("app.exe" if os.name == "nt" else "app")

        build(compiler, env, exe)
        first = objects(home)
        check(len(first) == 1 if ONE_UNIT else len(first) > 1,
              "the first build caches an object per unit")
        check(not list(home.glob("cache/objects/*/*.tmp")), "no temporary object is left")

        report = build(compiler, env, exe, "--time-passes")
        check(objects(home) == first, "a second build reuses every object")
        check(all(f"time: {phase} " in report for phase in ("hash", "objects", "link", "total")),
              "--time-passes reports the compiler and cache phases")

        bad = next(iter(first))
        bad.write_bytes(b"junk")
        build(compiler, env, exe)
        check(objects(home)[bad] == first[bad], "an object that is not one is compiled again")

        bad.write_bytes(b"\x7fELF" + b"\0" * 60)
        build(compiler, env, exe)
        check(True, "a link that fails with cached objects is retried without them")

        shutil.rmtree(home / "cache" / "objects")
        build(compiler, env, exe, "--no-cache")
        check(not objects(home), "--no-cache neither reads nor writes the cache")

        build(compiler, env, exe)
        size = sum(len(data) for data in objects(home).values())
        build(compiler, {**env, "OFIR_CACHE_LIMIT": str(size // 4)}, exe, "-g")
        left = sum(len(data) for data in objects(home).values())
        check(left <= size // 8, "OFIR_CACHE_LIMIT deletes objects down to half of it")

        before = set(objects(home))
        build(compiler, env, exe, "--release")
        added = set(objects(home)) - before
        check(len(added) == 1, "a release build is one unit, cached as one object")
        build(compiler, env, exe, "--release")
        check(set(objects(home)) - before == added, "a second release build reuses it")

        check_invalidation(compiler, env, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
