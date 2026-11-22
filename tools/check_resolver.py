#!/usr/bin/env python3
"""Checks how `ofir build` resolves git and registry dependencies (ADR
0039) where the package tests cannot reach:

- a registry that cannot be fetched, a package it does not have, versions
  listed out of order, only yanked versions, a checksum that differs from
  the index (E0914) and an archive that cannot be fetched;
- requirements on a repository with different `rev`s, versions out of
  order, no tags, a checksum that differs from the lock, a broken checkout
  and `--offline` with a repository that is not cached;
- a lock whose registry commit is not in the registry, with and without
  `--locked`, and `ofir update` of one registry package;
- rounds that move a version, move a tag to the same commit, and never
  settle;
- a root manifest that cannot be read, and a parent whose `workspace`
  has `members` that are not a list.

    python3 tools/check_resolver.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

from check_packages import (GIT_ENV, INDEX, LIB, MAIN, Ofir, expect, make_registry_fixtures,
                            manifest, write, ROOT)

UNREADABLE = "/proc/self/mem"   # Linux: reading it fails at the first byte


def git(path, *words):
    return subprocess.run(["git", "-C", str(path), *words], env=dict(os.environ, **GIT_ENV),
                          check=True, capture_output=True, text=True).stdout.strip()


def versions(path, tags):
    """A repository with a commit for each (tag, dependencies); a tag of
    None is left untagged."""
    path.mkdir(parents=True)
    git(path, "init", "-q")
    for tag, deps in tags:
        write(path / "ofir.json", f'{{"name": "{path.name}", "build": {{"kind": "lib"}}, '
              f'"dependencies": {deps}}}\n')
        write(path / "src" / "index.of", f"{LIB}// {tag}\n")
        git(path, "add", "-A")
        git(path, "commit", "-qm", "x")
        if tag:
            git(path, "tag", tag)
    return path


def dep(url, rest='"version": ">=0.1"'):
    return f'{{"git": "{Path(url).as_posix()}", {rest}}}'


def rev(url, tag):
    return dep(url, f'"rev": "{tag}"')


def exact(url, version):
    return dep(url, f'"version": "={version}"')


def app(tmp, name, deps):
    work = tmp / name
    write(work / "main.of", MAIN)
    write(work / "ofir.json", manifest("app", deps))
    return work


def registry(tmp):
    """An index whose `zz` lists 2.0.0 (a wrong checksum) before 1.0.0 (no
    archive), and whose `yy` has only a yanked version."""
    index, archives, source = tmp / "reg" / "index", tmp / "reg" / "archives", tmp / "reg" / "zz"
    archives.mkdir(parents=True)
    url = "file://" + archives.as_posix() + "/{name}-{version}.tar.gz"
    write(index / "config.json", json.dumps({"download": url}) + "\n")
    write(source / "ofir.json", '{"name": "zz", "build": {"kind": "lib"}}\n')
    write(source / "src" / "index.of", LIB)
    with tarfile.open(archives / "zz-2.0.0.tar.gz", "w:gz") as tar:
        for f in sorted(f for f in source.rglob("*") if f.is_file()):
            tar.add(f, arcname=f.relative_to(source).as_posix())
    line = {"dependencies": {}, "checksum": "0" * 64, "yanked": False}
    write(index / "2" / "zz", "".join(json.dumps(dict(line, version=v)) + "\n"
                                      for v in ("2.0.0", "1.0.0")))
    write(index / "2" / "yy", json.dumps(dict(line, version="1.0.0", yanked=True)) + "\n")
    for words in (["init", "-q"], ["add", "-A"], ["commit", "-qm", "x"]):
        git(index, *words)
    return index


def registries(ofir, tmp):
    index = registry(tmp)
    for n, (deps, message) in enumerate([
            ('{"zz": "^9"}', "its versions are 1.0.0, 2.0.0"),
            ('{"yy": "^1"}', "it has no versions that are not yanked"),
            ('{"zz": "=2.0.0"}', "the checksum of `zz` differs from the registry"),
            ('{"zz": "=1.0.0"}', "cannot fetch the dependency `zz`"),
            ('{"nosuch": "^1"}', "the registry `{}` has no package `nosuch`".format(index))]):
        work = app(tmp, f"r{n}", deps)
        expect(ofir(work, "build", "main.of", status=1, OFIR_REGISTRY=str(index)), message, deps)
    work = app(tmp, "unfetched", '{"zz": "^1"}')
    expect(ofir(work, "build", "main.of", status=1, OFIR_REGISTRY=str(tmp / "none")),
           "cannot fetch the registry", "a registry that cannot be fetched")


def repositories(ofir, tmp):
    far = versions(tmp / "repos" / "far", [("v0.10.0", "{}"), ("v0.9.0", "{}")])
    bare = versions(tmp / "repos" / "bare", [(None, "{}")])
    write(tmp / "b" / "src" / "index.of", LIB)
    write(tmp / "b" / "ofir.json", '{"name": "b", "build": {"kind": "lib"}, "dependencies": '
          f'{{"far": {rev(far, "v0.9.0")}}}}}\n')
    high = dep(far, '"version": "^5"')
    for name, deps, message in [
            ("mixed", f'{{"far": {rev(far, "v0.10.0")}, "b": {{"path": "../b"}}}}',
             "the requirements on it differ"),
            ("high", f'{{"far": {high}}}', "its versions are 0.9.0, 0.10.0"),
            ("untagged", f'{{"bare": {dep(bare)}}}', "it has no tags `vX.Y.Z`"),
            ("offline", f'{{"x": {dep(tmp / "repos" / "nowhere")}}}', "it is not in the cache")]:
        work = app(tmp, name, deps)
        words = ["--offline"] if name == "offline" else []
        expect(ofir(work, "build", *words, "main.of", status=1), message,
               f"a repository: {name}")
    work = app(tmp, "sum", f'{{"far": {dep(far)}}}')
    ofir(work, "fetch")
    lock = (work / "ofir.lock").read_text()
    sum_ = re.search(r'"checksum": "([0-9a-f]+)"', lock).group(1)
    write(work / "ofir.lock", lock.replace(sum_, "1" * 64))
    expect(ofir(work, "build", "main.of", status=1), "the checksum of `far` differs from",
           "a checksum that differs from the lock")
    (work / "ofir.lock").unlink()
    for checkout in (Path(ofir.env["OFIR_HOME"]) / "cache" / "src").iterdir():
        shutil.rmtree(checkout / ".git")
        (checkout / ".git").mkdir()
    expect(ofir(work, "build", "main.of", status=1), "is not a checkout", "a broken checkout")


def locks(ofir, tmp):
    far = versions(tmp / "repos" / "far", [("v0.1.0", "{}")])
    index = tmp / "index"
    shutil.copytree(INDEX, index)
    work = app(tmp, "idx", f'{{"json": "^1", "pp": "^0.1", "far": {dep(far)}}}')
    ofir(work, "fetch", OFIR_REGISTRY=str(index))
    lock = (work / "ofir.lock").read_text()
    write(work / "ofir.lock", lock.replace(git(index, "rev-parse", "HEAD"), "1" * 40))
    for words in (["--locked"], []):
        expect(ofir(work, "build", *words, "main.of", status=1, OFIR_REGISTRY=str(index)),
               "was rewritten", f"a lost registry commit with {words}")
    write(work / "ofir.lock", lock)
    ofir(work, "update", "pp", OFIR_REGISTRY=str(index))


def rounds(ofir, tmp):
    repos = tmp / "repos"
    for top_tags, name in [([("v0.1.0", "{}"), ("v0.1.1", "{}")], "top"),
                           ([("v0.1.0", "{}")], "same")]:
        top = versions(repos / name, top_tags)
        if name == "same":
            git(top, "tag", "v0.1.1")   # two versions of one commit
        mid = versions(repos / f"mid-{name}",
                       [("v0.1.0", f'{{"{name}": {exact(top, "0.1.0")}}}')])
        ofir(app(tmp, name, f'{{"{name}": {dep(top)}, "mid": {dep(mid)}}}'), "build", "main.of")
    # f and g require each other at versions that never settle; f 0.1.1
    # also requires h, which the last round has not resolved.
    f, g, h = repos / "f", repos / "g", versions(repos / "h", [("v0.1.0", "{}")])

    def on(name, repo, version, extra=""):
        return f'{{"{name}": {exact(repo, version)}{extra}}}'
    versions(f, [("v0.1.0", on("g", g, "0.1.0")),
                 ("v0.1.1", on("g", g, "0.2.0", f', "h": {dep(h)}'))])
    versions(g, [("v0.1.0", on("f", f, "0.1.1")), ("v0.2.0", on("f", f, "0.1.0"))])
    expect(ofir(app(tmp, "cycle", f'{{"f": {dep(f)}, "g": {dep(g)}}}'), "build", "main.of",
                status=1), "error[E0916]", "versions that never settle")


def unread(ofir, tmp):
    work = app(tmp, "unread", "")
    (work / "ofir.json").unlink()
    (work / "ofir.json").symlink_to(UNREADABLE)
    ofir(work, "build", "main.of")   # left for the manifest checks
    write(tmp / "ws" / "ofir.json", '{"workspace": {"members": 1}}\n')
    ofir(app(tmp / "ws", "member", ""), "build", "main.of")   # not a workspace of it


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    make_registry_fixtures(ROOT / "tests" / "registry", INDEX.parent)
    if os.name == "nt" or not Path(UNREADABLE).exists():
        print("resolver: skipped (needs git and /proc)")
        return
    for check in (registries, repositories, locks, rounds, unread):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            check(Ofir(compiler, tmp / "home"), tmp)
    print("resolver: ok")


if __name__ == "__main__":
    main()
