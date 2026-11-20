#!/usr/bin/env python3
"""Checks The Ofir Book (docs/book/, ADR 0095, OFIR-80).

- Chapters: docs/book/README.md links every `NN-name.md` of the book
  once, in order, and nothing else under `## Chapters`.
- Programs: every `ofir` block of a chapter has a `<!-- check: KIND -->`
  line before it, and is compiled as `main.of` (or the `file=NAME` of
  the check) in an empty directory:
  - `run`: `ofir run` exits with 0 and prints the first `text` block
    after it (before the next `ofir` block);
  - `error EXXXX`: `ofir check` fails and reports the code; a `text`
    block after it, if any, is the compiler's output;
  - `test [file=NAME]`: `ofir test` passes; a `text` block after it, if
    any, is its output, which names the tests after the file.
- Versions: every `ofir-X.Y.Z` and `ofir X.Y.Z` of the book is the
  compiler's version (`compiler/driver/version.of`), so the installation
  steps name the release the book follows.
- Codes: every `EXXXX` or `WXXXX` the book cites has an explanation in
  docs/errors/.

With `--compiler` the programs are compiled; without it only the text is
checked. With `--installed PREFIX` the compiler is `PREFIX/bin/ofir`, run
with `OFIR_STD` and `OFIR_RUNTIME` unset, as on a machine that only has
the release (`tools/check_install.py`).

    python3 tools/check_book.py --compiler build/stage1/ofir
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / "docs" / "book"
VERSION_FILE = ROOT / "compiler" / "driver" / "version.of"
CHAPTER = re.compile(r"^\d\d-[a-z-]+\.md$")
LINK = re.compile(r"^\d+\. \[[^\]]+\]\((\d\d-[a-z-]+\.md)\)$")
CHECK = re.compile(r"^<!-- check: (run|test|error) ?([EW]\d{4}|file=[a-z_]+\.of)? -->$")
FENCE = re.compile(r"^```(\w*)$")
VERSION = re.compile(r"\bofir[- ](\d+\.\d+\.\d+)\b")
CODE = re.compile(r"\b([EW]\d{4})\b")


def blocks(lines):
    """[(language, start line, text, check)] of the fenced blocks."""
    out, i = [], 0
    while i < len(lines):
        fence = FENCE.match(lines[i])
        if not fence:
            i += 1
            continue
        start, body = i + 1, []
        i += 1
        while i < len(lines) and lines[i] != "```":
            body.append(lines[i])
            i += 1
        check = CHECK.match(lines[start - 2]) if start >= 2 else None
        out.append((fence.group(1), start, "\n".join(body) + "\n", check))
        i += 1
    return out


def check_chapters(errors):
    found = sorted(p.name for p in BOOK.iterdir() if CHAPTER.match(p.name))
    listed, inside = [], False
    for line in (BOOK / "README.md").read_text().splitlines():
        if line.startswith("## "):
            inside = line == "## Chapters"
            continue
        if inside and line.strip():
            match = LINK.match(line)
            if not match:
                errors.append(f"README.md: `{line}` is not a numbered chapter link")
                continue
            listed.append(match.group(1))
    if listed != found:
        errors.append(f"README.md: ## Chapters lists {listed}, the book has {found}")
    return found


def check_text(name, text, version, errors):
    for match in VERSION.finditer(text):
        if match.group(1) != version:
            errors.append(f"{name}: names ofir {match.group(1)}, the compiler is {version}")
    for code in sorted(set(CODE.findall(text))):
        if not (ROOT / "docs" / "errors" / f"{code}.md").exists():
            errors.append(f"{name}: {code} has no explanation in docs/errors/")


def run(compiler, args, cwd, env):
    return subprocess.run([compiler, *args], cwd=cwd, env=env, capture_output=True,
                          text=True, stdin=subprocess.DEVNULL, timeout=300)


def check_program(compiler, env, where, kind, arg, program, expected, errors):
    code = arg if kind == "error" else None
    name = arg[len("file="):] if arg and arg.startswith("file=") else "main.of"
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / name).write_text(program)
        command = {"run": "run", "test": "test"}.get(kind, "check")
        proc = run(compiler, [command, name], tmp, env)
        output = (proc.stdout + proc.stderr).rstrip()
        if kind == "error":
            if proc.returncode == 0 or f"[{code}]" not in proc.stderr:
                errors.append(f"{where}: expected {code}, got:\n{output[-1500:]}")
                return
        elif proc.returncode != 0:
            errors.append(f"{where}: `ofir {command}` failed:\n{output[-1500:]}")
            return
        if kind == "run":
            output = proc.stdout.rstrip()
        if expected is not None and output != expected.rstrip():
            errors.append(f"{where}: the output differs:\n--- book\n{expected}--- compiler\n"
                          f"{output}")


def check_book(compiler, env, version, errors):
    chapters = check_chapters(errors)
    programs = 0
    for chapter in ["README.md"] + chapters:
        text = (BOOK / chapter).read_text()
        check_text(chapter, text, version, errors)
        found = blocks(text.splitlines())
        for index, (lang, line, body, check) in enumerate(found):
            if lang != "ofir":
                continue
            where = f"docs/book/{chapter}:{line}"
            if not check:
                errors.append(f"{where}: an `ofir` block needs a `<!-- check: ... -->` line")
                continue
            expected = None
            for other in found[index + 1:]:
                if other[0] == "ofir":
                    break
                if other[0] == "text":
                    expected = other[2]
                    break
            kind, arg = check.group(1), check.group(2)
            if (kind == "error") != bool(arg and not arg.startswith("file=")) or (
                    arg and arg.startswith("file=") and kind != "test"):
                errors.append(f"{where}: `{check.group(0)}` is not a valid check")
                continue
            if kind == "run" and expected is None:
                errors.append(f"{where}: a `run` program needs a `text` block with its output")
                continue
            programs += 1
            if compiler:
                check_program(compiler, env, where, kind, arg, body, expected,
                              errors)
    return chapters, programs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler")
    parser.add_argument("--installed")
    args = parser.parse_args()
    env = dict(os.environ)
    compiler = args.compiler
    if args.installed:
        exe = "ofir.exe" if os.name == "nt" else "ofir"
        compiler = str(Path(args.installed) / "bin" / exe)
        env.pop("OFIR_STD", None)
        env.pop("OFIR_RUNTIME", None)
    if compiler:
        compiler = str(Path(compiler).resolve())
    match = re.search(r'VERSION: str = "([^"]+)"', VERSION_FILE.read_text())
    version = match.group(1)
    errors = []
    chapters, programs = check_book(compiler, env, version, errors)
    for error in errors:
        print(f"book: {error}", file=sys.stderr)
    checked = "compiled" if compiler else "not compiled"
    print(f"book: {len(chapters)} chapters, {programs} programs ({checked}), "
          f"{len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
