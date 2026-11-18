#!/usr/bin/env python3
"""Runs the compatibility corpus (ADR 0084, OFIR-71).

Each directory `tests/compat/<major>.<minor>/` holds programs that the
release `<major>.<minor>.0` built and ran:

- `name.of` with `name.out`: it builds, and the program prints `name.out`
  and exits with 0; `name.args` holds its arguments, one per line;
- `name.of` with `name.err`: it fails to build, and every line of
  `name.err` is in what the compiler printed;
- `name_test.of` with `name_test.out`: `ofir test` prints `name_test.out`;
- any other file is read by those (a module, an input).

A program that no longer does what its corpus says is a break. It must be
listed in `tests/compat/breaks.json`, as `"<major>.<minor>/<file>": "<text>"`,
where `<text>` is in `CHANGELOG.md`; a listed break must still fail. With
`--release-branch`, a corpus of the compiler's own minor version takes no
breaks (a patch release breaks nothing).

    python3 tools/check_compat.py --compiler build/stage1/ofir
    python3 tools/check_compat.py --installed PREFIX

With `--installed`, the compiler is `PREFIX/bin/ofir` and runs with
`OFIR_STD` and `OFIR_RUNTIME` unset, from a copy of the corpus outside
the repository.
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

ROOT = Path(__file__).resolve().parent.parent
COMPAT = ROOT / "tests" / "compat"
EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 120


def run(cmd, cwd, env):
    try:
        out = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True,
                             timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return None
    return out


def check_program(compiler, work, name, env):
    """The reason `name` does not do what the corpus says, or None."""
    source = work / f"{name}.of"
    if name.endswith("_test"):
        out = run([compiler, "test", source.name], work, env)
        if out is None:
            return "`ofir test` timed out"
        if out.stdout != (work / f"{name}.out").read_text():
            return f"`ofir test` printed:\n{out.stdout}{out.stderr}"
        return None
    exe = work / f"{name}.compat{EXE}"
    build = run([compiler, "build", source.name, "-o", exe.name], work, env)
    if build is None:
        return "the build timed out"
    printed = build.stdout + build.stderr
    expected_err = work / f"{name}.err"
    if expected_err.exists():
        if build.returncode == 0:
            return "it builds, but the corpus expects an error"
        for line in expected_err.read_text().splitlines():
            if line and line not in printed:
                return f"the compiler did not print `{line}`:\n{printed}"
        return None
    if build.returncode != 0:
        return f"it does not build:\n{printed}"
    args_file = work / f"{name}.args"
    args = args_file.read_text().splitlines() if args_file.exists() else []
    out = run([str(exe)] + args, work, env)
    if out is None:
        return "the program timed out"
    if out.returncode != 0:
        return f"the program exited with {out.returncode}:\n{out.stdout}{out.stderr}"
    if out.stdout != (work / f"{name}.out").read_text():
        return f"the program printed:\n{out.stdout}"
    return None


def programs(corpus):
    names = []
    for source in sorted(corpus.glob("*.of")):
        name = source.stem
        if (corpus / f"{name}.out").exists() or (corpus / f"{name}.err").exists():
            names.append(name)
    return names


def compiler_minor(compiler, env):
    out = run([compiler, "version"], ROOT, env)
    match = re.search(r"(\d+)\.(\d+)\.\d+", out.stdout if out else "")
    return f"{match.group(1)}.{match.group(2)}" if match else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default=f"build/stage1/ofir{EXE}")
    parser.add_argument("--installed", help="the prefix the archive was installed in")
    parser.add_argument("--release-branch", action="store_true",
                        help="a corpus of the compiler's minor version takes no breaks")
    args = parser.parse_args()

    env = dict(os.environ)
    if args.installed:
        compiler = str(Path(args.installed).resolve() / "bin" / f"ofir{EXE}")
        env.pop("OFIR_STD", None)
        env.pop("OFIR_RUNTIME", None)
    else:
        compiler = str(Path(args.compiler).resolve())
    minor = compiler_minor(compiler, env)
    if minor is None:
        print(f"compat: `{compiler} version` names no version", file=sys.stderr)
        return 1

    breaks = json.loads((COMPAT / "breaks.json").read_text())
    changelog = (ROOT / "CHANGELOG.md").read_text()
    errors = []
    for key, text in breaks.items():
        if text not in changelog:
            errors.append(f"{key}: the break's entry `{text}` is not in CHANGELOG.md")
        if args.release_branch and key.split("/")[0] == minor:
            errors.append(f"{key}: a corpus of {minor} takes no breaks in a {minor} release")

    count = 0
    corpora = sorted(p for p in COMPAT.iterdir() if p.is_dir())
    with tempfile.TemporaryDirectory(prefix="ofir-compat-") as tmp:
        for corpus in corpora:
            work = Path(tmp) / corpus.name
            shutil.copytree(corpus, work)
            for name in programs(corpus):
                count += 1
                key = f"{corpus.name}/{name}.of"
                problem = check_program(compiler, work, name, env)
                if key in breaks:
                    if problem is None:
                        errors.append(f"{key}: listed as a break, but it passes: unlist it")
                elif problem is not None:
                    errors.append(f"{key}: {problem}")
    for key in breaks:
        corpus, _, file = key.partition("/")
        if not (COMPAT / corpus / file).exists():
            errors.append(f"{key}: listed as a break, but there is no such file")
    for error in errors:
        print(f"compat: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"compat: {count} programs in {len(corpora)} corpora, {len(breaks)} known breaks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
