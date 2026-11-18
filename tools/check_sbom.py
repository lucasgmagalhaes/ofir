#!/usr/bin/env python3
"""Checks `ofir sbom` (ADR 0081, OFIR-68).

- the CycloneDX document lists the root and each package of the lock, with
  its version, license, package URL and the lock's checksum, and the
  dependency graph;
- `--format=spdx` gives an SPDX 2.3 document of the same packages;
- the output is the same for the same lock, and dated only by
  `SOURCE_DATE_EPOCH`;
- a missing lock is E0913 and an unknown argument exits with 2.

    python3 tools/check_sbom.py --compiler build/stage1/ofir
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
import util

fn main() {
    println("{greet.hello()} {util.hello()}")
}
"""


def fail(message):
    print(f"sbom: {message}", file=sys.stderr)
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
        print("sbom: skipped (needs git and tar)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        index = tmp / "index"
        archives = tmp / "archives"
        env = dict(os.environ, OFIR_HOME=str(tmp / "home"), OFIR_REGISTRY=str(index),
                   GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
                   GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")
        env.pop("SOURCE_DATE_EPOCH", None)

        def git(*words):
            subprocess.run(["git", "-C", str(index), *words], env=env, check=True,
                           capture_output=True)

        def ofir(cwd, *words, ok=True, extra=None):
            result = subprocess.run([compiler, *words], cwd=cwd, env=dict(env, **(extra or {})),
                                    capture_output=True, text=True, timeout=600)
            if ok and result.returncode != 0:
                fail(f"{' '.join(words)} exited with {result.returncode}:\n"
                     f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
            return result

        index.mkdir()
        git("init", "-q")
        config = {"download": f"file://{archives}/{{name}}-{{version}}.tar.gz"}
        write(index / "config.json", json.dumps(config) + "\n")
        git("add", ".")
        git("commit", "-q", "-m", "init")

        lib = tmp / "greet"
        write(lib / "ofir.json", '{"name": "greet", "version": "1.0.0", "license": "MIT", '
              '"build": {"kind": "lib"}}\n')
        write(lib / "src" / "index.of", LIB)
        ofir(lib, "publish")
        util = tmp / "util"
        write(util / "ofir.json", '{"name": "util", "license": "Apache-2.0", '
              '"build": {"kind": "lib"}}\n')
        write(util / "src" / "index.of", LIB)

        app = tmp / "app"
        write(app / "ofir.json", '{"name": "app", "version": "0.3.0", "license": "MIT OR '
              'Apache-2.0", "build": {"kind": "exe", "root": "main.of"}, '
              '"dependencies": {"greet": "^1", "util": {"path": "../util"}}}\n')
        write(app / "main.of", APP)
        missing = ofir(app, "sbom", ok=False)
        if missing.returncode == 0 or "E0913" not in missing.stderr:
            fail(f"a missing lock is not E0913:\n{missing.stderr}")
        ofir(app, "fetch")
        lock = json.loads("\n".join((app / "ofir.lock").read_text().splitlines()[1:]))
        checksum = lock["packages"][0]["checksum"]

        text = ofir(app, "sbom").stdout
        if ofir(app, "sbom").stdout != text:
            fail("two runs over the same lock differ")
        bom = json.loads(text)
        if bom["bomFormat"] != "CycloneDX" or bom["specVersion"] != "1.5" \
                or "timestamp" in bom["metadata"]:
            fail(f"the document header is wrong:\n{text}")
        root = bom["metadata"]["component"]
        if (root["name"], root["version"], root["type"]) != ("app", "0.3.0", "application") \
                or root["licenses"] != [{"expression": "MIT OR Apache-2.0"}]:
            fail(f"the root is wrong: {root}")
        parts = {c["name"]: c for c in bom["components"]}
        if sorted(parts) != ["greet", "util"]:
            fail(f"the components are {sorted(parts)}")
        greet = parts["greet"]
        props = {p["name"]: p["value"] for p in greet["properties"]}
        if greet["version"] != "1.0.0" or greet["licenses"] != [{"expression": "MIT"}] \
                or not greet["purl"].startswith("pkg:ofir/greet@1.0.0?repository_url=") \
                or props != {"ofir:source": "registry", "ofir:checksum": checksum}:
            fail(f"greet is wrong: {greet}")
        local = parts["util"]
        if "version" in local or "purl" in local \
                or local["licenses"] != [{"expression": "Apache-2.0"}]:
            fail(f"util is wrong: {local}")
        graph = {d["ref"]: d["dependsOn"] for d in bom["dependencies"]}
        if sorted(graph[root["bom-ref"]]) != sorted([greet["bom-ref"], local["bom-ref"]]):
            fail(f"the dependency graph is wrong: {graph}")

        dated = ofir(app, "sbom", extra={"SOURCE_DATE_EPOCH": "1700000000"}).stdout
        if json.loads(dated)["metadata"].get("timestamp") != "2023-11-14T22:13:20Z":
            fail(f"SOURCE_DATE_EPOCH does not date the document:\n{dated}")

        spdx = json.loads(ofir(app, "sbom", "--format=spdx").stdout)
        names = {p["SPDXID"]: p for p in spdx["packages"]}
        described = names[spdx["documentDescribes"][0]]
        if spdx["spdxVersion"] != "SPDX-2.3" or described["name"] != "app" \
                or sorted(p["name"] for p in spdx["packages"]) != ["app", "greet", "util"]:
            fail(f"the SPDX document is wrong: {spdx}")
        greets = [p for p in spdx["packages"] if p["name"] == "greet"][0]
        if greets["licenseDeclared"] != "MIT" or checksum not in greets["comment"]:
            fail(f"greet is wrong in SPDX: {greets}")
        depends = [r for r in spdx["relationships"] if r["relationshipType"] == "DEPENDS_ON"]
        if len(depends) != 2:
            fail(f"the SPDX relationships are {spdx['relationships']}")

        if ofir(app, "sbom", "--format=xml", ok=False).returncode != 2:
            fail("an unknown format does not exit with 2")
    print("sbom: ok")


if __name__ == "__main__":
    main()
