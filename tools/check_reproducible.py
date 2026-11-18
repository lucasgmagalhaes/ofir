#!/usr/bin/env python3
"""Checks reproducible builds (ADR 0083, OFIR-70).

Builds the same project (with a dependency, a panic and `-g --trim-paths`)
twice, in two directories with different `HOME`, `OFIR_HOME`, file times
and umasks, and compares byte for byte:

- the `.ll` of `--emit=llvm`;
- the executable, and the record of `--record=FILE`;
- the site of `ofir doc` and the document of `ofir sbom`.

It also checks that the panic names its logical path and that no artifact
holds the directory it was built in.

    python3 tools/check_reproducible.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

UTIL = """/** The greeting. */
pub fn hello() str {
    return "hello"
}
"""

APP = """import util

/** Prints the greeting, then reads past the end of a list. */
fn main() {
    println(util.hello())
    const a = [1, 2]
    const i = 5
    println("{a[i]}")
}
"""


def fail(message):
    print(f"reproducible: {message}", file=sys.stderr)
    sys.exit(1)


def write(path, text, mtime):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    os.utime(path, (mtime, mtime))


def build(compiler, root, mtime, umask):
    """Builds the project under `root`; returns {artifact name: bytes}."""
    util = root / "util"
    app = root / "app"
    write(util / "ofir.json", '{"name": "util", "version": "1.2.0", "license": "MIT", '
          '"build": {"kind": "lib"}}\n', mtime)
    write(util / "src" / "index.of", UTIL, mtime)
    write(app / "ofir.json", '{"name": "app", "version": "0.1.0", "license": "MIT", '
          '"dependencies": {"util": {"path": "../util"}}}\n', mtime)
    write(app / "src" / "main.of", APP, mtime)
    env = dict(os.environ, HOME=str(root / "home"), OFIR_HOME=str(root / "ofir-home"),
               SOURCE_DATE_EPOCH="1700000000")

    def ofir(*words, ok=True):
        old = os.umask(umask)
        try:
            result = subprocess.run([compiler, *words], cwd=app, env=env,
                                    capture_output=True, timeout=600)
        finally:
            os.umask(old)
        if ok and result.returncode != 0:
            fail(f"{' '.join(words)} in {root.name} exited with {result.returncode}:\n"
                 f"{result.stderr.decode(errors='replace')[-2000:]}")
        return result

    out = app / "out"
    out.mkdir()
    ofir("build", "src/main.of", "-g", "--trim-paths", "--emit=llvm", "-o", "out/ll")
    ofir("build", "src/main.of", "-g", "--trim-paths", "--record=out/record.json",
         "-o", "out/app")
    run = subprocess.run([str(out / "app")], capture_output=True, text=True, timeout=60)
    if run.returncode != 101 or "at src/main.of:8:" not in run.stderr:
        fail(f"the panic does not name `src/main.of`:\n{run.stderr}")
    ofir("doc", "-o", "out/doc")
    sbom = ofir("sbom").stdout
    files = {"app.ll": (out / "ll.ll").read_bytes(), "app": (out / "app").read_bytes(),
             "record.json": (out / "record.json").read_bytes(), "sbom.json": sbom}
    for path in sorted((out / "doc").rglob("*")):
        if path.is_file():
            files[f"doc/{path.relative_to(out / 'doc')}"] = path.read_bytes()
    return files


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    if os.name == "nt":
        print("reproducible: skipped (POSIX only)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        first = build(compiler, tmp / "first-build", 1000000000, 0o022)
        second = build(compiler, tmp / "the-second-one", 1600000000, 0o077)
        if sorted(first) != sorted(second):
            fail(f"different artifacts: {sorted(first)} and {sorted(second)}")
        for name in sorted(first):
            if first[name] != second[name]:
                fail(f"`{name}` differs between the two builds")
            for root in ("first-build", "the-second-one", str(tmp)):
                if root.encode() in first[name] or root.encode() in second[name]:
                    fail(f"`{name}` holds the build directory `{root}`")
        record = json.loads(first["record.json"])
        paths = [source["path"] for source in record["sources"]]
        if "src/main.of" not in paths or "util/src/index.of" not in paths:
            fail(f"the record names unexpected sources: {paths[:4]}")
        if record["artifacts"][0]["path"] != "app":
            fail(f"the record names unexpected artifacts: {record['artifacts']}")
    print(f"reproducible: {len(first)} artifacts identical across two builds")


if __name__ == "__main__":
    main()
