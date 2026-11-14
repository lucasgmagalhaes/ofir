#!/usr/bin/env python3
"""Checks `ofir fmt` over the repository's Ofir sources (OFIR-56; the
formatter is described in docs/design/toolchain.md): for every file,

- **idempotence:** formatting the output again does not change it;
- **comments:** the output keeps every `//` comment of the input, in order;
- **semantics:** the output parses to the same AST (`--emit=ast`) as the
  input, up to the order of the imports, which the formatter sorts.

    python3 tools/check_fmt_corpus.py --compiler build/stage1/ofir [dir...]

With no directories it checks std, compiler, tests and examples.
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIRS = ["std", "compiler", "tests", "examples"]
COMMENT_RE = re.compile(r"//[^\n]*")


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace")


def comments(text: str) -> list[str]:
    """The `//` comments outside strings, trimmed."""
    found = []
    for line in text.splitlines():
        stripped = line.split('"')   # text between quotes is a string
        outside = "".join(stripped[0::2])
        found += [m.group(0).rstrip() for m in COMMENT_RE.finditer(outside)]
    return found


def normalized_ast(dump: str) -> list[str]:
    """The AST dump with the top-level imports sorted to the front."""
    lines = dump.splitlines()
    blocks: list[list[str]] = []
    for line in lines[1:]:
        if line.startswith("  ") and not line.startswith("   "):
            blocks.append([line])
        elif blocks:
            blocks[-1].append(line)
    imports = sorted(b for b in blocks if b[0].startswith("  import "))
    rest = [b for b in blocks if not b[0].startswith("  import ")]
    return lines[:1] + [line for b in imports + rest for line in b]


def check(compiler: str, path: Path, tmp: Path) -> list[str]:
    source = path.read_text(errors="replace")
    first = run([compiler, "fmt", "--stdout", str(path)])
    if first.returncode != 0:
        return []   # not an Ofir program the formatter accepts (a fail test)
    out = tmp / path.name
    out.write_text(first.stdout, newline="")   # no CRLF on Windows
    errors = []
    again = run([compiler, "fmt", "--stdout", str(out)])
    if again.stdout != first.stdout:
        errors.append("formatting the output again changes it")
    if comments(source) != comments(first.stdout):
        errors.append("the output drops, adds or reorders a comment")
    before = run([compiler, "build", str(path), "--emit=ast"])
    after = run([compiler, "build", str(out), "--emit=ast"])
    if before.returncode == 0 and normalized_ast(before.stdout) != normalized_ast(after.stdout):
        errors.append("the output parses to a different AST")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", default=str(ROOT / "build" / "stage1" / "ofir"))
    parser.add_argument("dirs", nargs="*", default=DIRS)
    args = parser.parse_args()
    files = [p for d in args.dirs for p in sorted((ROOT / d).rglob("*.of"))]
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for path in files:
            for error in check(args.compiler, path, Path(tmp)):
                print(f"{path.relative_to(ROOT)}: {error}")
                failed += 1
    print(f"{len(files)} files: {failed} problems")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
