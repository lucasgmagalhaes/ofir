#!/usr/bin/env python3
"""Checks how `ofir build` reads the packages of a project (ADR 0039, ADR
0077) where the package tests cannot reach:

- a fetched package whose native file, path dependency or root file is a
  link out of its checkout (E0924), and one with no `ofir.json` (E0911);
- an override with no `ofir.json`, a dependency with an empty name and
  `$OFIR_REGISTRY` with a credential (E0925);
- `trust.registries` that is not a list of strings and `devDependencies`
  that is not an object (E0910);
- a lock that cannot be written, which is a warning, and a registry
  package that `--locked` would move;
- `--locked` with a lock that names a package the project no longer has,
  from a manifest with no dependencies, and without one it has;
- a dependency whose name is a module of the root, and a manifest that
  cannot be read.

    python3 tools/check_packages.py --compiler build/stage1/ofir
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests" / "runner"))
from run_tests import make_registry_fixtures  # noqa: E402

INDEX = ROOT / "build" / "registry-fixtures" / "index"

GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t",
           "GIT_COMMITTER_EMAIL": "t@t"}
LIB = "pub fn one() int {\n    return 1\n}\n"
MAIN = "fn main() {\n}\n"


def fail(message):
    print(f"packages: {message}", file=sys.stderr)
    sys.exit(1)


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def manifest(name, deps="{}", rest=""):
    listed = f', "dependencies": {deps}' if deps else ""
    return f'{{"name": "{name}", "build": {{"kind": "exe", "root": "main.of"}}{listed}{rest}}}\n'



class Ofir:
    def __init__(self, compiler, home):
        self.compiler = compiler
        self.env = dict(os.environ, OFIR_HOME=str(home), **GIT_ENV)

    def __call__(self, cwd, *words, status=0, **extra):
        result = subprocess.run([self.compiler, *words], cwd=cwd, env=dict(self.env, **extra),
                                capture_output=True, text=True, timeout=600)
        if result.returncode != status:
            fail(f"{' '.join(words)} in {cwd.name} exited with {result.returncode}, not "
                 f"{status}:\n{result.stdout[-2000:]}{result.stderr[-2000:]}")
        return result.stdout + result.stderr


def expect(output, text, what):
    if text not in output:
        fail(f"{what}: {text!r} not in {output[-2000:]!r}")


def repo(path, files):
    """A git repository at `path` with `files` (a link when the text starts
    with `->`), tagged v0.1.0."""
    for name, text in files.items():
        target = path / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if text.startswith("->"):
            target.symlink_to(text[2:])
        else:
            target.write_text(text)
    for words in (["init", "-q"], ["add", "-A"], ["commit", "-qm", "x"], ["tag", "v0.1.0"]):
        subprocess.run(["git", "-C", str(path), *words], env=dict(os.environ, **GIT_ENV),
                       check=True, capture_output=True)
    return path


def checkouts(ofir, tmp):
    outside = tmp / "outside"
    write(outside / "x.c", "int x(void) { return 1; }\n")
    write(outside / "root.of", LIB)
    write(outside / "dep" / "ofir.json", '{"name": "dep", "build": {"kind": "lib"}}\n')
    write(outside / "dep" / "src" / "index.of", LIB)
    lib = '{{"name": "far", "build": {{"kind": "lib"{build}}}{deps}}}\n'
    cases = {
        "native": ({"ofir.json": lib.format(build=', "native": ["x.c"]', deps=""),
                    "src/index.of": LIB, "x.c": f"->{outside / 'x.c'}"},
                   "the native file `x.c` is outside the checkout"),
        "path": ({"ofir.json": lib.format(build="",
                                          deps=', "dependencies": {"dep": {"path": "dep"}}'),
                  "src/index.of": LIB, "dep": f"->{outside / 'dep'}"},
                 "the dependency `dep` is outside the checkout"),
        "root": ({"ofir.json": lib.format(build=', "root": "root.of"', deps=""),
                  "root.of": f"->{outside / 'root.of'}"},
                 "the root file of `far` is outside the checkout"),
        "bare": ({"src/index.of": LIB}, "the dependency `far` has no `ofir.json`"),
    }
    for name, (files, message) in cases.items():
        source = repo(tmp / "repos" / name, files)
        app = tmp / f"app-{name}"
        write(app / "ofir.json",
              manifest("app", f'{{"far": {{"git": "{source.as_posix()}", "version": "^0.1"}}}}'))
        write(app / "main.of", MAIN)
        out = ofir(app, "build", "main.of", status=1)
        expect(out, message, f"a fetched package with a {name} problem")


def manifests(ofir, tmp):
    write(tmp / "b" / "ofir.json", '{"name": "b", "build": {"kind": "lib"}}\n')
    write(tmp / "b" / "src" / "index.of", LIB)
    app = tmp / "app"
    write(app / "main.of", MAIN)
    write(app / "ofir.json", manifest("app", '{"t": {"path": "../b"}}',
                                      ', "overrides": {"t": {"path": "../none"}}'))
    expect(ofir(app, "build", "main.of", status=1), "cannot find the override of `t`",
           "an override with no manifest")
    write(app / "ofir.json", manifest("app", '{"": {"path": "../b"}}'))
    expect(ofir(app, "build", "main.of", status=1), "`` is not a valid package name",
           "a dependency with an empty name")
    write(app / "ofir.json", manifest("app", '{"x": "^1"}'))
    expect(ofir(app, "build", "main.of", status=1, OFIR_REGISTRY="https://u:p@example.com/i"),
           "error[E0925]: `OFIR_REGISTRY` holds a credential", "a registry with a credential")
    write(tmp / "c" / "ofir.json", '{"name": "c", "build": {"kind": "lib"}, '
          '"dependencies": {"b": {"path": "../b"}}}\n')
    write(tmp / "c" / "src" / "index.of", LIB)
    write(app / "b.of", "pub fn two() int {\n    return 2\n}\n")   # the root has a module `b`
    write(app / "ofir.json", manifest("app", '{"c": {"path": "../c"}}'))
    ofir(app, "build", "main.of")
    (app / "b.of").unlink()
    for command, rest, message in [
            ("build", ', "audit": {}, "trust": {"registries": [1]}',
             "`trust.registries` must be a list of strings"),
            ("test", ', "devDependencies": []', "`devDependencies` must be an object")]:
        write(app / "ofir.json", manifest("app", "{}", rest))
        expect(ofir(app, command, "main.of", status=1), message, f"a manifest with {rest}")
    unread = tmp / "unread"
    (unread / "ofir.json").mkdir(parents=True)   # it exists but cannot be read
    write(app / "ofir.json", manifest("app", '{"u": {"path": "../unread"}}'))
    ofir(app, "build", "main.of")


def locks(ofir, tmp):
    source = repo(tmp / "repos" / "near", {"ofir.json": '{"name": "near", "build": {"kind": '
                                           '"lib"}}\n', "src/index.of": LIB})
    dep = f'{{"near": {{"git": "{source.as_posix()}", "version": "^0.1"}}}}'
    app = tmp / "locks"
    write(app / "main.of", MAIN)
    write(app / "ofir.json", manifest("app", dep))
    (app / "ofir.lock").mkdir()
    expect(ofir(app, "build", "main.of"), "warning: cannot write", "a lock that is a directory")
    (app / "ofir.lock").rmdir()
    ofir(app, "fetch")
    locked = (app / "ofir.lock").read_text()
    write(app / "ofir.json", manifest("app", ""))
    expect(ofir(app, "build", "--locked", "main.of", status=1),
           "`near` is in it but no longer used", "a lock with a package the project no longer has")
    write(app / "ofir.lock", locked.replace(source.as_posix(), "/elsewhere"))
    write(app / "ofir.json", manifest("app", dep))
    expect(ofir(app, "build", "--locked", "main.of", status=1), "`near` is not in it",
           "a lock without a package the project has")
    write(app / "ofir.json", manifest("app", '{"json": "=1.0.0"}'))
    (app / "ofir.lock").unlink()
    ofir(app, "fetch", OFIR_REGISTRY=str(INDEX))
    write(app / "ofir.json", manifest("app", '{"json": "^1.2"}'))
    expect(ofir(app, "build", "--locked", "main.of", status=1, OFIR_REGISTRY=str(INDEX)),
           "`json` would move from 1.0.0 to 1.2.0", "a registry package the lock moves")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    make_registry_fixtures(ROOT / "tests" / "registry", INDEX.parent)
    if os.name == "nt":
        print("packages: skipped (needs links and git)")
        return
    for check in (checkouts, manifests, locks):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            check(Ofir(compiler, tmp / "home"), tmp)
    print("packages: ok")


if __name__ == "__main__":
    main()
