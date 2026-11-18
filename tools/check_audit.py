#!/usr/bin/env python3
"""Checks `ofir audit` (ADR 0081, OFIR-68).

- a locked registry package with no advisory passes, and one with an
  advisory that affects its version is E0920 with exit status 1, naming
  the lowest version that fixes it;
- a withdrawn advisory, or one that `audit.ignore` names, does not fail;
- a yanked version in the lock is a warning;
- `--offline` reads the cached index, `--message-format=json` prints the
  match as a JSON diagnostic, an invalid `audit` is E0910 and an unknown
  argument exits with 2.

    python3 tools/check_audit.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

LIB = """pub fn hello() str {
    return "hello"
}
"""

APP = """import greet

fn main() {
    println(greet.hello())
}
"""

ADVISORY = {"id": "OFIR-2026-0001", "versions": "<1.1.0", "severity": "high",
            "summary": "hello says too much", "url": "https://example.com/0001",
            "aliases": ["CVE-2026-1234"], "withdrawn": False}


def fail(message):
    print(f"audit: {message}", file=sys.stderr)
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
        print("audit: skipped (needs git and tar)")
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

        def ofir(cwd, *words):
            return subprocess.run([compiler, *words], cwd=cwd, env=env, capture_output=True,
                                  text=True, timeout=600)

        def advise(*entries):
            lines = "".join(json.dumps(dict(ADVISORY, **e)) + "\n" for e in entries)
            write(index / "advisories" / "gr" / "ee" / "greet", lines)
            git("add", ".")
            git("commit", "-q", "-m", "advisories")

        def audit(*words, status):
            result = ofir(app, "audit", *words)
            if result.returncode != status:
                fail(f"`audit {' '.join(words)}` exited with {result.returncode}, not "
                     f"{status}:\n{result.stdout}{result.stderr}")
            return result.stderr

        index.mkdir()
        git("init", "-q")
        config = {"download": f"file://{archives}/{{name}}-{{version}}.tar.gz"}
        write(index / "config.json", json.dumps(config) + "\n")
        git("add", ".")
        git("commit", "-q", "-m", "init")

        lib = tmp / "greet"
        write(lib / "src" / "index.of", LIB)
        for version in ["1.0.0", "1.1.0"]:
            write(lib / "ofir.json", f'{{"name": "greet", "version": "{version}", '
                  '"license": "MIT", "build": {"kind": "lib"}}\n')
            if ofir(lib, "publish").returncode != 0:
                fail(f"cannot publish greet {version}")

        app = tmp / "app"
        manifest = ('{"name": "app", "build": {"kind": "exe", "root": "main.of"}, '
                    '"dependencies": {"greet": "=1.0.0"}AUDIT}\n')
        write(app / "ofir.json", manifest.replace("AUDIT", ""))
        write(app / "main.of", APP)
        if ofir(app, "fetch").returncode != 0:
            fail("cannot fetch the app")

        clean = audit(status=0)
        if "1 registry packages checked, 0 advisories" not in clean:
            fail(f"the summary is wrong:\n{clean}")

        advise({})
        found = audit(status=1)
        for part in ["error[E0920]", "`greet` 1.0.0", "OFIR-2026-0001", "(high)",
                     "fixed in 1.1.0", "https://example.com/0001"]:
            if part not in found:
                fail(f"the match does not show `{part}`:\n{found}")
        line = json.loads(audit("--message-format=json", status=1).splitlines()[0])
        if line["code"] != "E0920" or line["severity"] != "error":
            fail(f"the JSON diagnostic is wrong: {line}")

        advise({"withdrawn": True}, {"id": "OFIR-2026-0002", "versions": ">=2.0.0"})
        audit(status=0)
        advise({}, {"id": "OFIR-2026-0003", "versions": "<1.0.0 || =1.0.0"})
        offline = audit("--offline", status=0)
        if "E0920" in offline:
            fail(f"`--offline` fetched the index:\n{offline}")
        if audit(status=1).count("error[E0920]") != 2:
            fail("two advisories do not give two matches")

        ignore = ', "audit": {"ignore": ["OFIR-2026-0001", "OFIR-2026-0003"]}'
        write(app / "ofir.json", manifest.replace("AUDIT", ignore))
        ignored = audit(status=0)
        if "ignored by `audit.ignore`" not in ignored or "2 ignored" not in ignored:
            fail(f"`audit.ignore` does not accept the advisories:\n{ignored}")

        if ofir(lib, "yank", "1.0.0").returncode != 0:
            fail("cannot yank greet 1.0.0")
        yanked = audit(status=0)
        if "warning: `greet` 1.0.0 is yanked" not in yanked:
            fail(f"a yanked version is not a warning:\n{yanked}")

        write(app / "ofir.json", manifest.replace("AUDIT", ', "audit": {"ignore": "all"}'))
        invalid = audit(status=1)
        if "E0910" not in invalid or "audit.ignore" not in invalid:
            fail(f"an invalid `audit` is not E0910:\n{invalid}")
        write(app / "ofir.json", manifest.replace("AUDIT", ""))
        audit("--fix", status=2)
    print("audit: ok")


if __name__ == "__main__":
    main()
