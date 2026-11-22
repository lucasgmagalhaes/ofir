#!/usr/bin/env python3
"""Checks package metadata and `ofir search` (ADR 0097, OFIR-82).

- `ofir publish` records `description`, `keywords`, `repository` and
  `toolchain` in the index line;
- invalid metadata is E0910, for `ofir publish` and for a build;
- `ofir search` finds a package by its name, description or keywords,
  ignoring case and needing every word, shows its highest version that is
  not yanked, an exact name first, marks a toolchain this compiler is not
  in, leaves out a package whose versions are all yanked, prints JSON
  lines with `--message-format=json` and exits with 1 when nothing
  matches;
- `--index` names another index, and one that cannot be read gives 1;
  lines that are not packages are passed over.

    python3 tools/check_search.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def fail(message):
    print(f"search: {message}", file=sys.stderr)
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
        print("search: skipped (needs git and tar)")
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

        def ofir(cwd, *words, ok=True):
            result = subprocess.run([compiler, *words], cwd=cwd, env=env,
                                    capture_output=True, text=True, timeout=600)
            if ok and result.returncode != 0:
                fail(f"{' '.join(words)} exited with {result.returncode}:\n"
                     f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
            return result

        def publish(name, version, **meta):
            lib = tmp / name
            manifest = {"name": name, "version": version, "license": "MIT",
                        "build": {"kind": "lib"}, **meta}
            write(lib / "ofir.json", json.dumps(manifest) + "\n")
            write(lib / "src" / "index.of", "pub fn answer() int {\n    return 42\n}\n")
            ofir(lib, "publish")
            return lib

        json_meta = {"description": "Parse and print JSON",
                     "keywords": ["json", "parser"],
                     "repository": "https://example.com/json",
                     "toolchain": ">=0.5.0"}
        publish("json", "1.0.0", **json_meta)
        publish("json", "1.1.0", **json_meta)
        lib = publish("json", "1.2.0", **json_meta)
        ofir(lib, "yank", "1.2.0")
        publish("jsonc", "0.3.0", description="JSON with comments", keywords=["json"])
        publish("yaml", "2.0.0", description="Read YAML documents",
                toolchain=">=9.0.0")
        # Listed after `json` in the index, but before it by name.
        publish("zip", "0.1.0", description="Zip JSON with comments")
        gone = publish("old-json", "0.1.0", description="An old JSON parser")
        ofir(gone, "yank", "0.1.0")

        line = json.loads((index / "js" / "on" / "json").read_text().splitlines()[0])
        for key, value in json_meta.items():
            if line.get(key) != value:
                fail(f"the index line has {key} = {line.get(key)!r}, not {value!r}")

        def search(*words, ok=True):
            return ofir(tmp, "search", *words, ok=ok)

        found = search("json").stdout.splitlines()
        want = ["json 1.1.0: Parse and print JSON", "jsonc 0.3.0: JSON with comments",
                "zip 0.1.0: Zip JSON with comments"]
        if found != want:
            fail(f"`search json` printed {found}, not {want}")
        found = search("YAML").stdout.splitlines()
        if len(found) != 1 or not found[0].startswith("yaml 2.0.0: Read YAML documents") \
                or "needs the toolchain `>=9.0.0`" not in found[0]:
            fail(f"`search YAML` printed {found}")
        found = search("parser", "json").stdout.splitlines()
        if found != ["json 1.1.0: Parse and print JSON"]:
            fail(f"`search parser json` printed {found}")
        found = search("comments").stdout.splitlines()
        if found != ["jsonc 0.3.0: JSON with comments", "zip 0.1.0: Zip JSON with comments"]:
            fail(f"`search comments` printed {found}")
        rows = [json.loads(row) for row in
                search("json", "--message-format=json").stdout.splitlines()]
        if [row["name"] for row in rows] != ["json", "jsonc", "zip"] \
                or rows[0]["keywords"] != ["json", "parser"] \
                or rows[0]["repository"] != "https://example.com/json" \
                or rows[0]["license"] != "MIT":
            fail(f"`search --message-format=json` printed {rows}")
        none = search("toml", ok=False)
        if none.returncode != 1 or "no package" not in none.stdout:
            fail(f"`search toml` exited with {none.returncode}: {none.stdout}")
        if search(ok=False).returncode != 2:
            fail("`search` without words did not exit with 2")
        if search("json", "--wide", ok=False).returncode != 2:
            fail("`search` with an unknown option did not exit with 2")
        for words in (["--offline", "--index", str(index)], [f"--index={index}"]):
            if search("parser", *words).stdout != "json 1.1.0: Parse and print JSON\n":
                fail(f"`search parser {' '.join(words)}` does not read that index")
        for where in (tmp / "nowhere", tmp / "empty"):
            if where.name == "empty":
                (where / "x").mkdir(parents=True)
                for words in (["init", "-q"], ["commit", "-q", "--allow-empty", "-m", "x"]):
                    subprocess.run(["git", "-C", str(where), *words], env=env, check=True)
            got = search("json", "--index", str(where), ok=False)
            if got.returncode != 1 or "cannot read the index" not in got.stderr:
                fail(f"`search` in {where.name} gave {got.returncode}: {got.stderr}")
        write(index / "3" / "o" / "odd", 'not json\n{"version": "x"}\n')   # lines it passes over
        git("add", ".")
        git("commit", "-q", "-m", "odd")
        if search("odd", ok=False).returncode != 1:
            fail("`search odd` found lines that are not packages")

        bad = tmp / "bad"
        for meta, key in [({"keywords": ["Json"]}, "keywords"),
                          ({"keywords": ["a", "b", "c", "d", "e", "f"]}, "keywords"),
                          ({"description": "x" * 161}, "description"),
                          ({"repository": "http://example.com"}, "repository")]:
            manifest = {"name": "bad", "version": "1.0.0", "license": "MIT",
                        "build": {"kind": "lib"}, **meta}
            write(bad / "ofir.json", json.dumps(manifest) + "\n")
            write(bad / "src" / "index.of", "pub fn f() {\n}\n")
            result = ofir(bad, "publish", ok=False)
            if result.returncode == 0 or "E0910" not in result.stderr \
                    or f"`{key}`" not in result.stderr:
                fail(f"publishing with {meta} is not E0910:\n{result.stderr}")
            manifest["build"] = {"kind": "exe", "root": "main.of"}
            write(bad / "ofir.json", json.dumps(manifest) + "\n")
            write(bad / "main.of", "fn main() {\n}\n")
            result = ofir(bad, "check", "main.of", ok=False)
            if result.returncode == 0 or "E0910" not in result.stderr:
                fail(f"checking with {meta} is not E0910:\n{result.stderr}")
    print("search: ok")


if __name__ == "__main__":
    main()
