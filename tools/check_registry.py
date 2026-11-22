#!/usr/bin/env python3
"""Checks the registry downloads of `ofir fetch` (ADR 0032) where the
package tests cannot reach:

- an index named by a `file://` URL, and lines of it that are not
  versions, which are passed over;
- a version that is not in the cache with `--offline`;
- an index with no `config.json`, with one that is not JSONC and with one
  that has no `download` URL;
- an archive that cannot be downloaded, one with no `ofir.json`, and no
  `curl` on the `PATH`.

    python3 tools/check_registry.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests" / "runner"))
from run_tests import make_registry_fixtures  # noqa: E402

FIXTURES = ROOT / "build" / "registry-fixtures"
GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t",
           "GIT_COMMITTER_EMAIL": "t@t", "GIT_CONFIG_GLOBAL": os.devnull}


def fail(message):
    print(f"registry: {message}", file=sys.stderr)
    sys.exit(1)


class Registry:
    def __init__(self, compiler, tmp):
        self.compiler, self.tmp = compiler, tmp
        self.index = tmp / "index"
        self.archives = tmp / "archives"
        shutil.copytree(FIXTURES / "index", self.index)
        shutil.copytree(FIXTURES / "archives", self.archives)
        self.config({"download": f"file://{self.archives.as_posix()}/{{name}}-{{version}}.tar.gz"})

    def commit(self, path, text):
        file = self.index / path
        if text is None:
            file.unlink()
        else:
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(text)
        for words in (["add", "--all"], ["commit", "--quiet", "--no-gpg-sign", "-m", "x"]):
            subprocess.run(["git", "-C", str(self.index), *words],
                           env=dict(os.environ, **GIT_ENV), check=True, capture_output=True)

    def config(self, value):
        self.commit("config.json", value if isinstance(value, str) else json.dumps(value) + "\n")

    def fetch(self, name, home, *words, status=0, index=None, path=None):
        work = self.tmp / f"app-{name}"
        work.mkdir(exist_ok=True)
        (work / "ofir.json").write_text(
            '{\n  "name": "app",\n  "build": {"kind": "exe", "root": "main.of"},\n'
            f'  "dependencies": {{"{name}": "^1.0"}},\n}}\n')
        (work / "main.of").write_text("fn main() {\n}\n")
        lock = work / "ofir.lock"
        if lock.exists():
            lock.unlink()
        env = dict(os.environ, OFIR_HOME=str(self.tmp / home),
                   OFIR_REGISTRY=index or str(self.index))
        if path is not None:
            env["PATH"] = path
        result = subprocess.run([self.compiler, "fetch", *words], cwd=work, env=env,
                                capture_output=True, text=True, timeout=600)
        if result.returncode != status:
            fail(f"fetch of {name} exited with {result.returncode}, not {status}:\n"
                 f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
        return result.stdout + result.stderr

    def refused(self, name, home, message, *words, **options):
        out = self.fetch(name, home, *words, status=1, **options)
        if message not in out:
            fail(f"fetch of {name}: {message!r} not in {out[-2000:]!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    if os.name == "nt" or not all(shutil.which(p) for p in ("git", "tar", "curl")):
        print("registry: skipped (needs git, tar and curl)")
        return
    make_registry_fixtures(ROOT / "tests" / "registry", FIXTURES)
    with tempfile.TemporaryDirectory() as tmp:
        r = Registry(compiler, Path(tmp))
        lines = (r.index / "js" / "on" / "json").read_text()
        r.commit("js/on/json", lines + 'not json\n{"version": "x"}\n')
        r.fetch("json", "home", index=r.index.as_uri())
        shutil.rmtree(r.tmp / "home" / "cache" / "src")
        r.refused("json", "home", "`--offline` was given", "--offline", index=r.index.as_uri())

        r.commit("no/fi/nofile", json.dumps({"version": "1.0.0", "dependencies": {},
                                              "checksum": "0", "yanked": False}) + "\n")
        r.refused("nofile", "home-missing", "nofile-1.0.0.tar.gz")
        with tarfile.open(r.archives / "nofile-1.0.0.tar.gz", "w:gz") as tar:
            readme = r.tmp / "README"
            readme.write_text("no manifest\n")
            tar.add(readme, arcname="README")
        r.refused("nofile", "home-nofile", "has no `ofir.json`")
        tools = r.tmp / "bin"
        tools.mkdir()
        (tools / "git").symlink_to(shutil.which("git"))
        r.refused("json", "home-nocurl", "cannot run `curl`", path=str(tools))

        for config, message in [(None, "has no `config.json`"),
                                ("{", "is not valid JSONC"),
                                ({}, "has no `download` URL")]:
            if config is None:
                r.commit("config.json", None)
            else:
                r.config(config)
            r.refused("json", f"home-{len(message)}", message)
    print("registry: ok")


if __name__ == "__main__":
    main()
