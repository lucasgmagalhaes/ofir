#!/usr/bin/env python3
"""Checks the dependency scopes and overrides of ADR 0077 (OFIR-65).

- `devDependencies` are locked by every command but in the graph only
  under `ofir test`, and a dependency's own are never read;
- a key in `dependencies` and `devDependencies` is E0917;
- a dependency with `"os"` is in the graph only for that OS, and an
  unknown `os` is E0910;
- an override replaces a dependency everywhere in the graph, and its
  package is not locked.

    python3 tools/check_sources.py --compiler build/stage1/ofir
"""

import argparse
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests" / "runner"))
from run_tests import make_git_fixtures  # noqa: E402

FIXTURES = ROOT / "build" / "git-fixtures"


def fail(message):
    print(f"sources: {message}", file=sys.stderr)
    sys.exit(1)


def run(cmd, cwd, home, ok=True):
    env = dict(os.environ, OFIR_HOME=str(home))
    result = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, timeout=600)
    if ok and result.returncode != 0:
        fail(f"{' '.join(cmd)} in {cwd.name} exited with {result.returncode}:\n"
             f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def manifest(name, kind, sections):
    root = ', "root": "main.of"' if kind == "exe" else ""
    out = f'{{\n  "name": "{name}",\n  "build": {{"kind": "{kind}"{root}}},\n'
    for section, deps in sections:
        entries = "".join(f'    "{key}": {value},\n' for key, value in deps)
        out += f'  "{section}": {{\n{entries}  }},\n'
    return out + "}\n"


def library(work, name, body, sections=()):
    write(work / name / "ofir.json", manifest(name, "lib", sections))
    write(work / name / "src" / "index.of", body)


def version_fn(text):
    return f'pub fn version() str {{\n    return "{text}"\n}}\n'


def check_dev(compiler, tmp):
    work = tmp / "dev"
    text = f'{{"git": "{(FIXTURES / "text").as_posix()}", "version": "^1.0"}}'
    library(work / "deps", "mid", version_fn("mid"),
            [("devDependencies", [("ghost", '{"path": "../missing"}')])])
    write(work / "ofir.json", manifest("app", "exe", [
        ("dependencies", [("mid", '{"path": "deps/mid"}')]),
        ("devDependencies", [("text", text)])]))
    write(work / "main.of", "import mid\n\nfn main() {\n    println(mid.version())\n}\n")
    write(work / "main_test.of", "import std.testing.assert\nimport text\n\n@test\n"
          "fn textIsThere() {\n    assert(text.version() == \"1.1.0\")\n}\n")
    home = tmp / "home-dev"
    built = run([compiler, "run", "main.of"], work, home)
    if built.stdout.strip() != "mid":
        fail(f"the program printed {built.stdout!r}")
    if '"name": "text"' not in (work / "ofir.lock").read_text():
        fail("the dev dependency is not locked")
    tested = run([compiler, "test", "main.of"], work, home)
    if "textIsThere ... ok" not in tested.stdout:
        fail(f"the test of the dev dependency did not pass:\n{tested.stdout}")
    write(work / "main.of", "import text\n\nfn main() {\n    println(text.version())\n}\n")
    if run([compiler, "build", "main.of"], work, home, ok=False).returncode == 0:
        fail("the program imported a dev dependency")

    twice = tmp / "twice"
    library(twice / "deps", "mid", version_fn("mid"))
    write(twice / "ofir.json", manifest("app", "exe", [
        ("dependencies", [("mid", '{"path": "deps/mid"}')]),
        ("devDependencies", [("mid", '{"path": "deps/mid"}')])]))
    write(twice / "main.of", "fn main() {\n}\n")
    err = run([compiler, "build", "main.of"], twice, home, ok=False).stderr
    if err.count("error[E0917]") != 1 or "`mid` is declared twice" not in err:
        fail(f"a key in both lists is not one E0917:\n{err[-2000:]}")
    print("sources: dev dependencies ok")


def host_os():
    return {"Darwin": "macos", "Windows": "windows"}.get(platform.system(), "linux")


def check_os(compiler, tmp):
    work = tmp / "os"
    other = "windows" if host_os() != "windows" else "linux"
    library(work / "deps", "here", version_fn("here"))
    library(work / "deps", "there", "this does not parse\n")
    write(work / "ofir.json", manifest("app", "exe", [("dependencies", [
        ("here", f'{{"path": "deps/here", "os": "{host_os()}"}}'),
        ("there", f'{{"path": "deps/there", "os": "{other}"}}')])]))
    write(work / "main.of", "import here\n\nfn main() {\n    println(here.version())\n}\n")
    home = tmp / "home-os"
    built = run([compiler, "run", "main.of"], work, home)
    if built.stdout.strip() != "here":
        fail(f"the program printed {built.stdout!r}")
    write(work / "ofir.json", manifest("app", "exe", [("dependencies", [
        ("here", '{"path": "deps/here", "os": "beos"}')])]))
    err = run([compiler, "build", "main.of"], work, home, ok=False).stderr
    if "error[E0910]" not in err or "`os` must be" not in err:
        fail(f"an unknown `os` is not E0910:\n{err[-2000:]}")
    print("sources: target dependencies ok")


def check_overrides(compiler, tmp):
    work = tmp / "overrides"
    text = f'{{"git": "{(FIXTURES / "text").as_posix()}", "version": "^1.0"}}'
    library(work / "deps", "mid", 'import text\n\npub fn version() str {\n'
            '    return "mid {text.version()}"\n}\n', [("dependencies", [("text", text)])])
    library(work / "patched", "text", version_fn("patched"))
    sections = [("dependencies", [("mid", '{"path": "deps/mid"}'), ("text", text)]),
                ("overrides", [("text", '{"path": "patched/text"}')])]
    write(work / "ofir.json", manifest("app", "exe", sections))
    write(work / "main.of", "import mid\nimport text\n\nfn main() {\n"
          "    println(mid.version())\n    println(text.version())\n}\n")
    home = tmp / "home-overrides"
    built = run([compiler, "run", "main.of"], work, home)
    if built.stdout.split("\n")[:2] != ["mid patched", "patched"]:
        fail(f"the override is not used everywhere: {built.stdout!r}")
    lock = work / "ofir.lock"
    if lock.exists() and '"name": "text"' in lock.read_text():
        fail("an overridden package is locked")
    if (home / "cache" / "git").exists():
        fail("an overridden git package was fetched")

    sections[1] = ("overrides", [("text", '"patched/text"')])
    write(work / "ofir.json", manifest("app", "exe", sections))
    err = run([compiler, "build", "main.of"], work, home, ok=False).stderr
    if "the override of `text` needs a `path`" not in err:
        fail(f"an override without a path is not E0910:\n{err[-2000:]}")
    print("sources: overrides ok")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    FIXTURES.mkdir(parents=True, exist_ok=True)
    make_git_fixtures(ROOT / "tests" / "git", FIXTURES)
    with tempfile.TemporaryDirectory() as tmp:
        check_dev(compiler, Path(tmp))
        check_os(compiler, Path(tmp))
        check_overrides(compiler, Path(tmp))
    return 0


if __name__ == "__main__":
    sys.exit(main())
