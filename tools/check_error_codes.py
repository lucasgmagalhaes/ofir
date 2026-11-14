#!/usr/bin/env python3
"""Checks the error code registry (docs/errors/README.md).

- every code in the registry table appears once;
- every code annotated in tests/ (`// error[E0300]: ...`) is registered;
- every explanation file (docs/errors/EXXXX.md) is for a registered code,
  starts with `# EXXXX: ` and has `## Example` and `## Fix` sections;
- every registered code that is not `planned` has an explanation file;
- every code the compilers name (`"E0300"` in compiler/ and bootstrap/src)
  is registered, and every registered code that is not `planned` is named
  by one of them.

With `--compiler <ofir>` the `ofir` blocks of the explanation files are
compiled: those under `## Example` must report their code, those under
`## Fix` must compile without errors and without it.

The runner self-test fixtures (tests/runner/) are ignored.
"""

import argparse
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs" / "errors" / "README.md"
ROW_RE = re.compile(r"^\|\s*([EW]\d{4})\s*\|", re.MULTILINE)
USE_RE = re.compile(r"(?:error|warning)\[([EW]\d{4})\]")
FILE_RE = re.compile(r"^[EW]\d{4}\.md$")
BLOCK_RE = re.compile(r"^```ofir\n(.*?)^```$", re.MULTILINE | re.DOTALL)
PLANNED_RE = re.compile(r"^\|\s*([EW]\d{4})\s*\|.*\|\s*planned\s*\|\s*$", re.MULTILINE)


def unknown_codes(known: set) -> list[str]:
    problems = []
    for path in sorted((ROOT / "tests").rglob("*.of")):
        if "runner" in path.relative_to(ROOT / "tests").parts:
            continue
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), start=1):
            problems += [f"{path.relative_to(ROOT)}:{number}: {code} is not in "
                         f"{REGISTRY.relative_to(ROOT)}"
                         for code in USE_RE.findall(line) if code not in known]
    return problems


LITERAL_RE = re.compile(r'"([EW]\d{4})"')
SOURCES = [("compiler", "*.of"), ("bootstrap/src", "*.[ch]")]


def unsynchronized(known: set, planned: set) -> list[str]:
    """The codes the compilers name but the registry lacks, and the codes it
    has that no compiler names."""
    named: dict[str, Path] = {}
    for directory, pattern in SOURCES:
        for path in sorted((ROOT / directory).rglob(pattern)):
            for code in LITERAL_RE.findall(path.read_text(errors="replace")):
                named.setdefault(code, path)
    problems = [f"{named[code].relative_to(ROOT)}: {code} is not in "
                f"{REGISTRY.relative_to(ROOT)}" for code in sorted(set(named) - known)]
    problems += [f"{code} is registered but no compiler reports it; mark it `planned`"
                 for code in sorted(known - planned - set(named))]
    return problems


def explanation_files() -> list[Path]:
    return sorted(p for p in REGISTRY.parent.iterdir() if FILE_RE.match(p.name))


def sections(text: str) -> dict[str, str]:
    """The text of each `## ` section, by heading."""
    found = {}
    parts = re.split(r"^## (.*)$", text, flags=re.MULTILINE)
    for i in range(1, len(parts) - 1, 2):
        found[parts[i].strip()] = parts[i + 1]
    return found


def explanation_problems(path: Path, known: set) -> list[str]:
    code = path.stem
    name = path.relative_to(ROOT)
    text = path.read_text()
    problems = []
    if code not in known:
        problems.append(f"{name}: {code} is not in {REGISTRY.relative_to(ROOT)}")
    if not text.startswith(f"# {code}: "):
        problems.append(f"{name}: the first line must be `# {code}: <message>`")
    found = sections(text)
    problems += [f"{name}: no `## {heading}` section"
                 for heading in ("Example", "Fix") if heading not in found]
    return problems


def compile_block(compiler: str, source: str, tmp: Path) -> str:
    src = tmp / "example.of"
    src.write_text(source, newline="\n")
    proc = subprocess.run([compiler, "build", str(src), "--message-format=short",
                           "-o", str(tmp / "example")],
                          capture_output=True, text=True, timeout=60)
    return proc.stdout + proc.stderr


def example_problems(path: Path, compiler: str, tmp: Path) -> list[str]:
    code = path.stem
    name = path.relative_to(ROOT)
    found = sections(path.read_text())
    problems = []
    for heading, wanted in (("Example", True), ("Fix", False)):
        for n, block in enumerate(BLOCK_RE.findall(found.get(heading, "")), start=1):
            output = compile_block(compiler, block, tmp)
            reported = f"[{code}]" in output
            if wanted and not reported:
                problems.append(f"{name}: {heading} block {n} does not report {code}:\n"
                                f"{output.strip()}")
            if not wanted and (reported or "error[" in output):
                problems.append(f"{name}: {heading} block {n} does not compile cleanly:\n"
                                f"{output.strip()}")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", help="compile the examples with this ofir")
    args = parser.parse_args()

    registered = ROW_RE.findall(REGISTRY.read_text())
    problems = [f"{code} is registered {n} times"
                for code, n in Counter(registered).items() if n > 1]
    known = set(registered)
    problems += unknown_codes(known)
    files = explanation_files()
    for path in files:
        problems += explanation_problems(path, known)
    explained = {path.stem for path in files}
    planned = set(PLANNED_RE.findall(REGISTRY.read_text()))
    problems += [f"{code} has no explanation file docs/errors/{code}.md"
                 for code in sorted(known - planned - explained)]
    problems += unsynchronized(known, planned)
    if args.compiler and not problems:
        with tempfile.TemporaryDirectory() as tmp:
            for path in files:
                problems += example_problems(path, args.compiler, Path(tmp))
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    if problems:
        return 1
    checked = ", examples compiled" if args.compiler else ""
    print(f"error codes: {len(known)} registered, all test annotations known, "
          f"in sync with the compilers, "
          f"{len(files)} explained{checked}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
