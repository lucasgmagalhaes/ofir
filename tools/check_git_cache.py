#!/usr/bin/env python3
"""Checks the git cache of `ofir fetch` (ADR 0039) where the package tests
cannot reach:

- an `$OFIR_HOME` it cannot create the cache in;
- a checkout that is not one, and one at another commit, which it moves
  to the locked commit;
- a clone whose tags cannot be listed, and a tag `v...` that is not a
  version, which it passes over;
- no `git` on the `PATH`, with which a clone has no tags.

    python3 tools/check_git_cache.py --compiler build/stage1/ofir
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests" / "runner"))
from run_tests import make_git_fixtures  # noqa: E402

FIXTURES = ROOT / "build" / "git-fixtures"
GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t",
           "GIT_COMMITTER_EMAIL": "t@t"}


def fail(message):
    print(f"git-cache: {message}", file=sys.stderr)
    sys.exit(1)


def fetch(compiler, work, home, status=0, path=None):
    env = dict(os.environ, OFIR_HOME=str(home))
    if path is not None:
        env["PATH"] = path
    result = subprocess.run([compiler, "fetch"], cwd=work, env=env, capture_output=True,
                            text=True, timeout=600)
    if result.returncode != status:
        fail(f"fetch in {work.name} exited with {result.returncode}, not {status}:\n"
             f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result.stdout + result.stderr


def expect(output, text, what):
    if text not in output:
        fail(f"{what}: {text!r} not in {output[-2000:]!r}")


def git(repo, *words):
    subprocess.run(["git", "-C", str(repo), *words], env=dict(os.environ, **GIT_ENV),
                   check=True, capture_output=True)


def project(work, name, url, version):
    work.mkdir(parents=True)
    (work / "ofir.json").write_text(
        f'{{\n  "name": "app",\n  "build": {{"kind": "exe", "root": "main.of"}},\n'
        f'  "dependencies": {{"{name}": {{"git": "{url}", "version": "{version}"}}}},\n}}\n')
    (work / "main.of").write_text("fn main() {\n}\n")


def homes(compiler, tmp):
    work = tmp / "app"
    project(work, "text", (FIXTURES / "text").as_posix(), "^1.0")
    taken = tmp / "taken"
    taken.write_text("")
    expect(fetch(compiler, work, taken, status=1), "cannot create", "a home that is a file")
    home = tmp / "home"
    fetch(compiler, work, home)
    (checkout,) = (home / "cache" / "src").iterdir()
    git(checkout, "checkout", "--quiet", "--detach", "v1.0.0")
    fetch(compiler, work, home)
    head = subprocess.run(["git", "-C", str(checkout), "describe", "--tags"], capture_output=True,
                          text=True).stdout.strip()
    if head != "v1.1.0":
        fail(f"a checkout at another commit was left at {head!r}")
    shutil.rmtree(checkout / ".git")
    (checkout / ".git").mkdir()
    expect(fetch(compiler, work, home, status=1), "is not a checkout", "a checkout that is not one")
    shutil.rmtree(checkout)
    expect(fetch(compiler, work, home, status=1, path=str(tmp / "nowhere")),
           "it has no tags", "no git")


def tags(compiler, tmp):
    repo = tmp / "odd"
    (repo / "src").mkdir(parents=True)
    (repo / "ofir.json").write_text('{\n  "name": "odd",\n  "build": {"kind": "lib"},\n}\n')
    (repo / "src" / "index.of").write_text("pub fn one() int {\n    return 1\n}\n")
    for words in (["init", "-q"], ["add", "-A"], ["commit", "-qm", "x"], ["tag", "vnext"],
                  ["tag", "v0.1.0"]):
        git(repo, *words)
    work = tmp / "odd-app"
    project(work, "odd", repo.as_posix(), "^0.1")
    home = tmp / "home-odd"
    expect(fetch(compiler, work, home), "odd 0.1.0", "a tag that is not a version")
    (clone,) = (home / "cache" / "git").iterdir()
    (work / "ofir.lock").unlink()
    shutil.rmtree(clone)
    clone.mkdir()
    (clone / "HEAD").write_text("")
    expect(fetch(compiler, work, home, status=1), "error", "a clone whose tags cannot be listed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    if shutil.which("git") is None:
        print("git-cache: skipped (needs git)")
        return
    FIXTURES.mkdir(parents=True, exist_ok=True)
    make_git_fixtures(ROOT / "tests" / "git", FIXTURES)
    for check in (homes, tags):
        with tempfile.TemporaryDirectory() as tmp:
            check(compiler, Path(tmp))
    print("git-cache: ok")


if __name__ == "__main__":
    main()
