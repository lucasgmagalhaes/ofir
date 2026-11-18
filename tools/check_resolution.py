#!/usr/bin/env python3
"""Checks the contracts of dependency resolution (OFIR-64, ADR 0076).

On a graph of git and path dependencies, where two requirements on one
package (`^1.0` and `~1.0`) meet at 1.0.2:

- fetching writes `ofir.lock`, sorted by name, with that version;
- fetching again without changes leaves the lock untouched;
- fetching with the keys of every manifest reversed, from an empty cache,
  writes the same lock byte for byte;
- `--locked` accepts the lock;
- a cycle of path dependencies is E0916, with a note naming the cycle.

    python3 tools/check_resolution.py --compiler build/stage1/ofir
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests" / "runner"))
from run_tests import make_git_fixtures  # noqa: E402

FIXTURES = ROOT / "build" / "git-fixtures"


def fail(message):
    print(f"resolution: {message}", file=sys.stderr)
    sys.exit(1)


def run(cmd, cwd, home, ok=True):
    env = dict(os.environ, OFIR_HOME=str(home))
    result = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, timeout=600)
    if ok and result.returncode != 0:
        fail(f"{' '.join(cmd)} exited with {result.returncode}:\n"
             f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result


def manifest(name, kind, deps):
    lines = [f'  "{key}": {value},' for key, value in deps]
    root = ', "root": "main.of"' if kind == "exe" else ""
    return (f'{{\n  "name": "{name}",\n  "build": {{"kind": "{kind}"{root}}},\n'
            f'  "dependencies": {{\n' + "\n".join(f"  {line}" for line in lines) + "\n  },\n}\n")


def write_graph(work, reverse):
    text = (FIXTURES / "text").as_posix()
    shade = (FIXTURES / "shade").as_posix()
    app = [("text", f'{{"git": "{text}", "version": "^1.0"}}'),
           ("mid", '{"path": "deps/mid"}'),
           ("shade", f'{{"git": "{shade}", "rev": "v0.1.0"}}')]
    mid = [("words", f'{{"git": "{text}", "version": "~1.0"}}'),
           ("tint", f'{{"git": "{shade}", "rev": "v0.1.0"}}')]
    if reverse:
        app.reverse()
        mid.reverse()
    (work / "ofir.json").write_text(manifest("app", "exe", app))
    (work / "main.of").write_text("import mid\nimport shade\nimport text\n\nfn main() {\n"
                                  "    println(text.version())\n    println(mid.version())\n"
                                  "    println(shade.version())\n}\n")
    src = work / "deps" / "mid" / "src"
    src.mkdir(parents=True, exist_ok=True)
    (work / "deps" / "mid" / "ofir.json").write_text(manifest("mid", "lib", mid))
    (src / "index.of").write_text("import tint\nimport words\n\npub fn version() str {\n"
                                  "    return \"{words.version()} {tint.version()}\"\n}\n")


def check_lock(compiler, tmp):
    first = tmp / "first"
    first.mkdir()
    write_graph(first, False)
    run([compiler, "fetch"], first, tmp / "home1")
    lock = (first / "ofir.lock").read_text()
    names = [line.split('"name": "')[1].split('"')[0] for line in lock.splitlines()
             if '"name": "' in line]
    if names != sorted(names) or names != ["shade", "text"]:
        fail(f"the lock lists {names}, expected shade and text in order:\n{lock}")
    if '"version": "1.0.2"' not in lock:
        fail(f"text is not at 1.0.2:\n{lock}")
    stamp = (first / "ofir.lock").stat().st_mtime_ns
    run([compiler, "fetch"], first, tmp / "home1")
    if (first / "ofir.lock").stat().st_mtime_ns != stamp:
        fail("fetching again rewrote the lock")
    run([compiler, "build", "main.of", "--locked"], first, tmp / "home1")

    second = tmp / "second"
    second.mkdir()
    write_graph(second, True)
    run([compiler, "fetch"], second, tmp / "home2")
    again = (second / "ofir.lock").read_text()
    if again != lock:
        fail(f"reversed manifests give another lock:\n{lock}\n---\n{again}")
    print("resolution: lock ok")


def check_cycle(compiler, tmp):
    work = tmp / "cycle"
    for name, other in [("a", "b"), ("b", "a")]:
        src = work / "deps" / name / "src"
        src.mkdir(parents=True)
        (work / "deps" / name / "ofir.json").write_text(
            manifest(name, "lib", [(other, f'{{"path": "../{other}"}}')]))
        (src / "index.of").write_text(f"pub fn {name}() int {{\n    return 1\n}}\n")
    (work / "ofir.json").write_text(manifest("app", "exe", [("a", '{"path": "deps/a"}')]))
    (work / "main.of").write_text("import a\n\nfn main() {\n    println(\"{a.a()}\")\n}\n")
    result = run([compiler, "build", "main.of"], work, tmp / "home3", ok=False)
    if result.returncode == 0:
        fail("a cycle of dependencies was accepted")
    err = result.stderr
    if err.count("error[E0916]") != 1 or "the dependency `a` makes a cycle" not in err:
        fail(f"the cycle is not one E0916 at `a`:\n{err[-2000:]}")
    if "`a` -> `b` -> `a`" not in err:
        fail(f"the note does not name the cycle:\n{err[-2000:]}")
    print("resolution: cycle ok")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    FIXTURES.mkdir(parents=True, exist_ok=True)
    make_git_fixtures(ROOT / "tests" / "git", FIXTURES)
    with tempfile.TemporaryDirectory() as tmp:
        check_lock(compiler, Path(tmp))
        check_cycle(compiler, Path(tmp))
    return 0


if __name__ == "__main__":
    sys.exit(main())
