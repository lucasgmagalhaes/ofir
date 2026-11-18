#!/usr/bin/env python3
"""Checks trust roots and the pinned index commit (ADR 0081, OFIR-68).

- `ofir.lock` records the commit of the index; `fetch` keeps it while the
  locked versions fit, `update` records the new head, and a lock without
  `indexes` still passes `fetch --locked`;
- an index whose history was rewritten is E0922 on `update`, naming both
  commits, and `update --index-reset` accepts it;
- with `trust`, a registry or git source it does not list is E0921, one
  it lists is accepted, and an invalid `trust` is E0910.

    python3 tools/check_trust.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

LIB = """pub fn hello() str {
    return "hello"
}
"""


def fail(message):
    print(f"trust: {message}", file=sys.stderr)
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
        print("trust: skipped (needs git and tar)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp).resolve()
        index = tmp / "index"
        archives = tmp / "archives"
        env = dict(os.environ, OFIR_HOME=str(tmp / "home"), OFIR_REGISTRY=str(index),
                   GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
                   GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")

        def git(cwd, *words):
            return subprocess.run(["git", "-C", str(cwd), *words], env=env, check=True,
                                  capture_output=True, text=True).stdout.strip()

        def ofir(cwd, *words, status=0):
            result = subprocess.run([compiler, *words], cwd=cwd, env=env, capture_output=True,
                                    text=True, timeout=600)
            if result.returncode != status:
                fail(f"`{' '.join(words)}` exited with {result.returncode}, not {status}:\n"
                     f"{result.stdout}{result.stderr}")
            return result.stderr

        def read_lock():
            text = (app / "ofir.lock").read_text()
            return json.loads("".join(l for l in text.splitlines(True) if not l.startswith("//")))

        def locked_commit():
            lock = read_lock()
            pins = lock.get("indexes", [])
            if len(pins) != 1 or pins[0]["url"] != str(index):
                fail(f"the lock does not pin the index: {pins}")
            return pins[0]["commit"]

        index.mkdir()
        git(index, "init", "-q")
        config = {"download": f"file://{archives}/{{name}}-{{version}}.tar.gz"}
        write(index / "config.json", json.dumps(config) + "\n")
        git(index, "add", ".")
        git(index, "commit", "-q", "-m", "init")

        lib = tmp / "greet"
        write(lib / "src" / "index.of", LIB)

        def publish(version):
            write(lib / "ofir.json", f'{{"name": "greet", "version": "{version}", '
                  '"license": "MIT", "build": {"kind": "lib"}}\n')
            ofir(lib, "publish")

        publish("1.0.0")
        first = git(index, "rev-parse", "HEAD")

        shade = tmp / "shade"
        write(shade / "src" / "index.of", LIB)
        write(shade / "ofir.json", '{"name": "shade", "build": {"kind": "lib"}}\n')
        git(shade, "init", "-q", "-b", "main")
        git(shade, "add", ".")
        git(shade, "commit", "-q", "-m", "init")

        app = tmp / "app"
        manifest = ('{"name": "app", "build": {"kind": "exe", "root": "main.of"}, '
                    '"dependencies": {"greet": "^1.0.0"DEPS}TRUST}\n')

        def project(deps="", trust=""):
            write(app / "ofir.json", manifest.replace("DEPS", deps).replace("TRUST", trust))

        project()
        write(app / "main.of", "fn main() {\n}\n")
        ofir(app, "fetch")
        if locked_commit() != first:
            fail("`fetch` does not record the head of the index")

        publish("1.1.0")
        second = git(index, "rev-parse", "HEAD")
        ofir(app, "fetch")
        ofir(app, "fetch", "--locked")
        if locked_commit() != first:
            fail("`fetch` moved the index commit while the locked version fits")
        ofir(app, "update")
        if locked_commit() != second or '"version": "1.1.0"' not in (app / "ofir.lock").read_text():
            fail("`update` does not record the new head with the new version")

        text = (app / "ofir.lock").read_text()
        text = re.sub(r',\n  "indexes": \[[^\]]*\]', "", text)
        if "indexes" in text:
            fail("cannot drop `indexes` from the lock")
        write(app / "ofir.lock", text)
        ofir(app, "fetch", "--locked")
        ofir(app, "fetch")
        if locked_commit() != second:
            fail("a lock without `indexes` does not get one")

        git(index, "reset", "-q", "--hard", first)
        write(index / "config.json", json.dumps(config, indent=1) + "\n")
        git(index, "commit", "-q", "-am", "rewritten")
        third = git(index, "rev-parse", "HEAD")
        rewritten = ofir(app, "update", status=1)
        for part in ["error[E0922]", second, third, "--index-reset"]:
            if part not in rewritten:
                fail(f"a rewritten index does not show `{part}`:\n{rewritten}")
        ofir(app, "update", "--index-reset")
        if locked_commit() != third:
            fail("`update --index-reset` does not record the new history")
        ofir(app, "fetch", "--index-reset", status=2)

        git_dep = f', "shade": {{"git": "{shade}", "rev": "main"}}'
        project(git_dep, f', "trust": {{"registries": ["{index}"], "git": ["{tmp}/"]}}')
        ofir(app, "fetch")
        project(git_dep, f', "trust": {{"registries": ["{index}"]}}')
        untrusted = ofir(app, "fetch", status=1)
        if "error[E0921]" not in untrusted or "trust.git" not in untrusted:
            fail(f"an untrusted git source is not E0921:\n{untrusted}")
        project("", ', "trust": {"registries": ["https://example.com/index"]}')
        untrusted = ofir(app, "fetch", status=1)
        if "error[E0921]" not in untrusted or "trust.registries" not in untrusted:
            fail(f"an untrusted registry is not E0921:\n{untrusted}")
        project("", ', "trust": {"git": "https://github.com/acme/"}')
        invalid = ofir(app, "fetch", status=1)
        if "E0910" not in invalid or "trust.git" not in invalid:
            fail(f"an invalid `trust` is not E0910:\n{invalid}")
    print("trust: ok")


if __name__ == "__main__":
    main()
