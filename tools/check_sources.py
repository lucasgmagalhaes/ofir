#!/usr/bin/env python3
"""Checks the dependency scopes and overrides of ADR 0077 (OFIR-65).

- `devDependencies` are locked by every command but in the graph only
  under `ofir test`, and a dependency's own are never read;
- a key in `dependencies` and `devDependencies` is E0917;
- a dependency with `"os"` is in the graph only for that OS, and an
  unknown `os` is E0910;
- an override replaces a dependency everywhere in the graph, and its
  package is not locked;
- `ofir vendor` copies the locked packages, and with the overrides it
  prints the build holds `--offline` with an empty `$OFIR_HOME`; it stops
  on a manifest error and on a copy it cannot remove or write;
- the members of a workspace share the lock and the `overrides` of its
  root, and a member's own `overrides` are not read;
- `fetch --offline` and `update <name>` hold the lock, and `fetch`
  without a manifest gives 2.

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


def check_vendor(compiler, tmp):
    work = tmp / "vendor"
    text = f'{{"git": "{(FIXTURES / "text").as_posix()}", "version": "^1.0"}}'
    write(work / "ofir.json", manifest("app", "exe", [("dependencies", [("text", text)])]))
    write(work / "main.of", "import text\n\nfn main() {\n    println(text.version())\n}\n")
    home = tmp / "home-vendor"
    out = run([compiler, "vendor"], work, home).stdout
    if '"text": {"path": "vendor/text"}' not in out:
        fail(f"vendor printed {out!r}")
    copy = work / "vendor" / "text"
    if not (copy / "ofir.json").exists() or (copy / ".git").exists():
        fail("the package was not copied without `.git`")
    again = run([compiler, "vendor", "--offline", "third_party"], work, home).stdout
    if '"text": {"path": "third_party/text"}' not in again:
        fail(f"vendor into a named directory printed {again!r}")
    run([compiler, "vendor", "--offline", "third_party"], work, home)
    if not (work / "third_party" / "text" / "ofir.json").exists():
        fail("vendoring over an earlier copy lost the package")
    for words, message in [(["--bad"], "unexpected argument `--bad`"),
                           (["a", "b"], "unexpected argument `b`")]:
        bad = run([compiler, "vendor", *words], work, home, ok=False)
        if bad.returncode != 2 or message not in bad.stderr:
            fail(f"vendor {' '.join(words)}: {bad.returncode} {bad.stderr[-2000:]}")
    bare = tmp / "vendor-bare"
    bare.mkdir()
    bad = run([compiler, "vendor"], bare, home, ok=False)
    if bad.returncode != 2 or "needs an `ofir.json`" not in bad.stderr:
        fail(f"vendor without a manifest: {bad.returncode} {bad.stderr[-2000:]}")
    write(work / "ofir.json", manifest("app", "exe", [
        ("dependencies", [("text", text)]),
        ("overrides", [("text", '{"path": "vendor/text"}')])]))
    built = run([compiler, "run", "--offline", "main.of"], work, tmp / "home-empty")
    if built.stdout.strip() != "1.1.0":
        fail(f"the vendored build printed {built.stdout!r}")
    print("sources: vendoring ok")


def check_vendor_failures(compiler, tmp):
    """`ofir vendor` stops on a manifest error and on a copy it cannot
    remove or write; links stand in for the files it cannot handle."""
    work = tmp / "vendor-fail"
    text = f'{{"git": "{(FIXTURES / "text").as_posix()}", "version": "^1.0"}}'
    write(work / "ofir.json", manifest("app", "exe", [("dependencies", [("text", text)])]))
    home = tmp / "home-vendor-fail"
    run([compiler, "vendor"], work, home)

    def refused(message, *words):
        bad = run([compiler, "vendor", "--offline", *words], work, home, ok=False)
        if bad.returncode != 1 or message not in bad.stderr:
            fail(f"vendor {' '.join(words)}: {bad.returncode} {bad.stderr[-2000:]}")

    write(work / "taken", "")   # a file where the directory goes
    refused("cannot copy `text` into `taken/text`", "taken")
    if os.name != "nt":
        kept = tmp / "kept"
        write(kept / "a.txt", "a")
        link = work / "vendor" / "text" / "linked"
        link.symlink_to(kept, target_is_directory=True)   # removing it leaves the link
        refused("cannot remove `vendor/text`")
        link.unlink()
        repo = tmp / "linked-repo"   # a package with a link to nothing, which it cannot copy
        library(repo.parent, repo.name, version_fn("1"))
        (repo / "src" / "sub").mkdir()
        (repo / "src" / "sub" / "dangling").symlink_to(tmp / "nowhere")
        env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
                   GIT_COMMITTER_EMAIL="t@t")
        for words in (["init", "-q"], ["add", "-A"], ["commit", "-qm", "x"], ["tag", "v0.1.0"]):
            subprocess.run(["git", "-C", str(repo), *words], env=env, check=True)
        linked = f'{{"git": "{repo.as_posix()}", "version": "^0.1"}}'
        write(work / "ofir.json", manifest("app", "exe", [("dependencies", [("linked", linked)])]))
        bad = run([compiler, "vendor"], work, home, ok=False)
        if bad.returncode != 1 or "cannot copy `linked` into `vendor/linked`" not in bad.stderr:
            fail(f"vendor of a link to nothing: {bad.returncode} {bad.stderr[-2000:]}")
    write(work / "ofir.json", '{\n  "name": "app",\n  "dependencies": []\n}\n')
    bad = run([compiler, "vendor"], work, home, ok=False)
    if bad.returncode != 1 or "error" not in bad.stderr:
        fail(f"vendor with a bad manifest: {bad.returncode} {bad.stderr[-2000:]}")
    print("sources: vendoring failures ok")


def check_workspace(compiler, tmp):
    work = tmp / "workspace"
    text = f'{{"git": "{(FIXTURES / "text").as_posix()}", "version": "^1.0"}}'
    write(work / "ofir.json", '{\n  "workspace": {"members": ["app", "tool"]}\n}\n')
    write(work / "app" / "ofir.json", manifest("app", "exe", [
        ("dependencies", [("text", text)]),
        ("overrides", [("text", '{"path": "../missing"}')])]))
    write(work / "app" / "main.of",
          "import text\n\nfn main() {\n    println(text.version())\n}\n")
    write(work / "tool" / "ofir.json", manifest("tool", "exe", [
        ("dependencies", [("shade", text.replace("text", "shade").replace("^1.0", "^0.2"))])]))
    write(work / "tool" / "main.of", "fn main() {\n}\n")
    home = tmp / "home-workspace"
    built = run([compiler, "run", "main.of"], work / "app", home)
    if built.stdout.strip() != "1.1.0":
        fail(f"the member printed {built.stdout!r}")
    if (work / "app" / "ofir.lock").exists():
        fail("a member wrote its own lock")
    lock = (work / "ofir.lock").read_text()
    if '"name": "text"' not in lock or '"name": "shade"' not in lock:
        fail(f"the workspace lock does not hold every member's packages:\n{lock}")
    fetched = run([compiler, "fetch", "--locked"], work, home)
    if "text 1.1.0" not in fetched.stdout:
        fail(f"fetch at the workspace root printed {fetched.stdout!r}")

    library(work / "patched", "text", version_fn("patched"))
    write(work / "ofir.json", '{\n  "workspace": {"members": ["app", "tool"]},\n'
          '  "overrides": {"text": {"path": "patched/text"}}\n}\n')
    built = run([compiler, "run", "main.of"], work / "app", home)
    if built.stdout.strip() != "patched":
        fail(f"the root's override is not used in a member: {built.stdout!r}")
    print("sources: workspaces ok")


def check_fetch(compiler, tmp):
    work = tmp / "fetch"
    text = f'{{"git": "{(FIXTURES / "text").as_posix()}", "version": "^1.0"}}'
    write(work / "ofir.json", manifest("app", "exe", [("dependencies", [("text", text)])]))
    write(work / "main.of", "fn main() {\n}\n")
    home = tmp / "home-fetch"
    run([compiler, "fetch"], work, home)
    lock = (work / "ofir.lock").read_text()
    for words in (["fetch", "--offline"], ["update", "text", "--offline"]):
        run([compiler, *words], work, home)
        if (work / "ofir.lock").read_text() != lock:
            fail(f"{' '.join(words)} changed the lock")
    bare = tmp / "fetch-bare"
    bare.mkdir()
    bad = run([compiler, "fetch"], bare, home, ok=False)
    if bad.returncode != 2 or "needs an `ofir.json`" not in bad.stderr:
        fail(f"fetch without a manifest: {bad.returncode} {bad.stderr[-2000:]}")
    print("sources: fetching ok")


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
        check_vendor(compiler, Path(tmp))
        check_vendor_failures(compiler, Path(tmp))
        check_workspace(compiler, Path(tmp))
        check_fetch(compiler, Path(tmp))
    return 0


if __name__ == "__main__":
    sys.exit(main())
