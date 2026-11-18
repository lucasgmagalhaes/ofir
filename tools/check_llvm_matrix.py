#!/usr/bin/env python3
"""Checks that the LLVM range is the one of `tools/llvm.json` everywhere
(ADR 0084, OFIR-71).

- the `llvm` matrix of the CI and Release workflows is `[oldest, newest]`,
  and the Release workflow's other LLVM steps use `newest`;
- each `clang N or later`, `LLVM N or later` and `clang N to M` in the
  README, the toolchain design, spec section 13 and E0902 names `oldest`
  (and `newest`).

    python3 tools/check_llvm_matrix.py
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

WORKFLOWS = [".github/workflows/ci.yml", ".github/workflows/release.yml"]
DOCS = ["README.md", "docs/design/toolchain.md", "docs/spec/13-memory-layout.md",
        "docs/errors/E0902.md"]

MATRIX = re.compile(r'^\s*llvm: \["(\d+)", "(\d+)"\]', re.M)
NEWEST = re.compile(r"llvm\.sh (\d+)$|matrix\.llvm == '(\d+)'", re.M)
RANGE = re.compile(r"\b(?:clang|LLVM) (\d+) (?:or later|to (\d+|the latest release))")


def main():
    spec = json.loads((ROOT / "tools/llvm.json").read_text())
    oldest, newest = str(spec["oldest"]), str(spec["newest"])
    errors = []
    for name in WORKFLOWS:
        text = (ROOT / name).read_text()
        matrices = MATRIX.findall(text)
        if not matrices:
            errors.append(f"{name}: no `llvm` matrix")
        for found in matrices:
            if found != (oldest, newest):
                errors.append(f"{name}: the matrix is {list(found)}, not [{oldest}, {newest}]")
        for match in NEWEST.finditer(text):
            version = match.group(1) or match.group(2)
            if version != newest:
                errors.append(f"{name}: `{match.group(0)}` is not the newest LLVM, {newest}")
    for name in DOCS:
        text = (ROOT / name).read_text()
        for match in RANGE.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            low, high = match.group(1), match.group(2)
            if low != oldest:
                errors.append(f"{name}:{line}: `{match.group(0)}`: the oldest LLVM is {oldest}")
            if high and high.isdigit() and high != newest:
                errors.append(f"{name}:{line}: `{match.group(0)}`: the newest LLVM is {newest}")
    for error in errors:
        print(f"llvm: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"llvm: LLVM {oldest} to {newest} in {len(WORKFLOWS)} workflows and {len(DOCS)} docs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
