#!/usr/bin/env python3
"""Checks publishing to a registry (ADR 0079, OFIR-67).

- `ofir publish` appends the version to a clone of the index, with the
  checksum of the archive's files, writes the archive where a `file://`
  download URL points and commits; a project then builds with it;
- `--dry-run` leaves the index as it is;
- a version is published once, and a bad name, a bad version or a path
  dependency is E0919;
- a package without a `license` is E0919 and one with an invalid SPDX
  expression is E0910; the license is recorded in the index (ADR 0081);
- `ofir yank` keeps a yanked version out of `ofir update`, and `--undo`
  brings it back.

    python3 tools/check_publish.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

LIB = """pub fn version() str {{
    return "{version}"
}}
"""

APP = """import greet

fn main() {
    println("greet {greet.version()}")
}
"""


def fail(message):
    print(f"publish: {message}", file=sys.stderr)
    sys.exit(1)


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    if os.name == "nt" or shutil.which("git") is None or shutil.which("tar") is None:
        print("publish: skipped (needs git and tar)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        index = tmp / "index"
        archives = tmp / "archives"
        env = dict(os.environ, OFIR_HOME=str(tmp / "home"), OFIR_REGISTRY=str(index),
                   GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
                   GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")

        def git(*words):
            subprocess.run(["git", "-C", str(index), *words], env=env, check=True,
                           capture_output=True)

        index.mkdir()
        git("init", "-q")
        config = {"download": f"file://{archives}/{{name}}-{{version}}.tar.gz"}
        write(index / "config.json", json.dumps(config) + "\n")
        git("add", ".")
        git("commit", "-q", "-m", "init")

        lib = tmp / "greet"

        def ofir(cwd, *words, ok=True):
            result = subprocess.run([compiler, *words], cwd=cwd, env=env,
                                    capture_output=True, text=True, timeout=600)
            if ok and result.returncode != 0:
                fail(f"{' '.join(words)} exited with {result.returncode}:\n"
                     f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
            return result

        def library(version, name="greet", deps="{}", license='"license": "MIT", '):
            write(lib / "ofir.json", f'{{"name": "{name}", "version": "{version}", {license}'
                  f'"build": {{"kind": "lib"}}, "dependencies": {deps}}}\n')
            write(lib / "src" / "index.of", LIB.format(version=version))

        def refused(words, message):
            result = ofir(lib, *words, ok=False)
            if result.returncode == 0 or "E0919" not in result.stderr \
                    or message not in result.stderr:
                fail(f"{' '.join(words)} is not E0919 ({message}):\n{result.stderr}")

        entry = index / "gr" / "ee" / "greet"
        library("1.0.0")
        ofir(lib, "publish", "--dry-run")
        if entry.exists():
            fail("--dry-run wrote to the index")
        ofir(lib, "publish")
        lines = [json.loads(line) for line in entry.read_text().splitlines()]
        if [line["version"] for line in lines] != ["1.0.0"] or lines[0]["yanked"] \
                or lines[0].get("license") != "MIT":
            fail(f"the index has {lines}")
        if not (archives / "greet-1.0.0.tar.gz").exists():
            fail("the archive was not written")
        packaged = ofir(lib, "package").stdout
        if f"checksum {lines[0]['checksum']}" not in packaged:
            fail(f"`package` and the index disagree:\n{packaged}{lines}")
        refused(["publish"], "already published")

        library("1.1.0")
        ofir(lib, "publish")
        log = subprocess.run(["git", "-C", str(index), "log", "--format=%s"], env=env,
                             capture_output=True, text=True).stdout
        if "Publish greet 1.1.0" not in log or "Publish greet 1.0.0" not in log:
            fail(f"the publishes were not committed:\n{log}")

        app = tmp / "app"
        write(app / "ofir.json", '{"name": "app", "build": {"kind": "exe", '
              '"root": "main.of"}, "dependencies": {"greet": "^1"}}\n')
        write(app / "main.of", APP)

        def chosen():
            ofir(app, "update")
            return ofir(app, "run", "--no-cache", "main.of").stdout.strip()

        if chosen() != "greet 1.1.0":
            fail("the newest version was not chosen")
        ofir(lib, "yank", "1.1.0")
        if chosen() != "greet 1.0.0":
            fail("a yanked version was chosen")
        again = ofir(lib, "yank", "1.1.0").stdout
        if "already yanked" not in again:
            fail(f"yanking twice: {again}")
        ofir(lib, "yank", "1.1.0", "--undo")
        if chosen() != "greet 1.1.0":
            fail("`yank --undo` did not bring the version back")
        unknown = ofir(lib, "yank", "9.9.9", ok=False)
        if unknown.returncode == 0 or "E0919" not in unknown.stderr:
            fail(f"yanking an unknown version is not E0919:\n{unknown.stderr}")

        library("1.2.0", name="Greet")
        refused(["publish"], "package name")
        library("1.2")
        refused(["publish"], "`X.Y.Z`")
        library("1.2.0", deps='{"other": {"path": "../other"}}')
        refused(["publish"], "path or git")
        library("1.2.0", license="")
        refused(["publish"], "has no `license`")
        library("1.2.0", license='"license": "MIT OR Nope", ')
        bad = ofir(lib, "publish", ok=False)
        if bad.returncode == 0 or "E0910" not in bad.stderr or "`Nope`" not in bad.stderr:
            fail(f"an invalid license is not E0910:\n{bad.stderr}")

        # The command lines and the states that stop a command before it starts.
        def usage(words, message, code=2, cwd=lib, environ=env):
            result = subprocess.run([compiler, *words], cwd=cwd, env=environ,
                                    capture_output=True, text=True, timeout=600)
            if result.returncode != code or message not in result.stderr:
                fail(f"{' '.join(words)}: not exit {code} with {message!r}:\n"
                     f"{result.returncode} {result.stderr}")

        library("1.3.0", deps='{"one": "^1", "two": "^2"}')
        dry = ofir(lib, "publish", "--dry-run").stdout
        if "would publish greet 1.3.0" not in dry:
            fail(f"a dry run with registry dependencies: {dry}")
        library("1.3.0")
        usage(["package", "x"], "unexpected argument `x` for `package`")
        usage(["publish", "--x"], "unexpected argument `--x` for `publish`")
        usage(["yank", "--x"], "unexpected argument `--x` for `yank`")
        usage(["yank"], "`yank` needs a version")
        usage(["publish", "--index", str(tmp)], "is not a clone of an index")
        usage(["yank", "1.3.0", f"--index={tmp}"], "is not a clone of an index")
        usage(["publish"], "is a URL", environ=dict(env, OFIR_REGISTRY="https://example.com/i"))
        local = {k: v for k, v in env.items() if k != "OFIR_REGISTRY"}
        usage(["publish"], "give a clone of the index", environ=local)
        empty = tmp / "empty"
        empty.mkdir()
        usage(["yank", "1.0.0"], "needs an `ofir.json` with a `name`", cwd=empty)
        usage(["package"], "needs an `ofir.json` in the current directory", 1, empty)
        write(empty / "ofir.json", "{\n")
        usage(["publish"], "`ofir.json` is not valid JSONC", 1, empty)

        # An index file with a line that is not JSON and no final newline,
        # and an index whose downloads are not `file://` URLs.
        entry.write_text(entry.read_text() + "not json")
        write(index / "config.json", '{"download": "https://example.com/{name}"}\n')
        git("commit", "-q", "-am", "remote downloads")
        published = ofir(lib, "publish", f"--index={index}").stdout
        if "upload " not in published:
            fail(f"a publish to a remote download URL: {published}")
        ofir(lib, "yank", "--index", str(index), "1.3.0")
        if '"version": "1.3.0"' not in entry.read_text() or "not json" not in entry.read_text():
            fail(f"the index after the yank:\n{entry.read_text()}")
    print("publish: ok")


if __name__ == "__main__":
    main()
