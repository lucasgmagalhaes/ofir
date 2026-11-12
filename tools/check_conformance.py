#!/usr/bin/env python3
"""Checks the conformance matrix (docs/design/conformance.md, OFIR-01).

The matrix has one row for each `## N.M` section of docs/spec, in the order
of the specification:

    | Section | Stage 0 | Stage 1 | Evidence | Gaps |

- Section: the heading's number and title, without a `**[planned]**` mark.
- Stage 0, Stage 1: `full`, `partial`, `none`, or `n/a` for a section that
  states no rule a compiler implements.
- Evidence: paths in backticks, relative to the repository root, that must
  exist (tests, or the sources that implement the section). A row needs
  one unless both stages are `n/a` or `none`.
- Gaps: `-`, or one or more gaps separated by `;`, each starting with its
  class: `subset:` (left out of stage 0 by docs/bootstrap.md), `limit:`
  (reported as E0901), `planned:` (marked **[planned]** in the
  specification), `missing:` (stated by the specification, yet absent),
  `untested:` (implemented, with no test of its own) or `target:`
  (depends on the platform). A stage that is `partial` or `none` needs a
  gap that says why.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / "docs" / "design" / "conformance.md"
SPEC = ROOT / "docs" / "spec"
STATES = {"full", "partial", "none", "n/a"}
CLASSES = ("subset:", "limit:", "planned:", "missing:", "untested:", "target:")
HEADING_RE = re.compile(r"^## (\d+\.\d+) (.*?)\s*(\*\*\[planned\]\*\*)?\s*$")
ROW_RE = re.compile(r"^\| (\d+\.\d+) ")
PATH_RE = re.compile(r"`([^`]+)`")


def spec_sections():
    sections = []
    for path in sorted(SPEC.glob("[0-9][0-9]-*.md")):
        for line in path.read_text(encoding="utf-8").splitlines():
            match = HEADING_RE.match(line)
            if match:
                sections.append(f"{match.group(1)} {match.group(2)}")
    return sections


def check_row(line_no, cells, errors):
    section, stage0, stage1, evidence, gaps = cells
    where = f"{MATRIX.relative_to(ROOT)}:{line_no}: {section.split(' ')[0]}"
    for state in (stage0, stage1):
        if state not in STATES:
            errors.append(f"{where}: unknown state `{state}`")
    paths = PATH_RE.findall(evidence)
    for path in paths:
        if not (ROOT / path).exists():
            errors.append(f"{where}: `{path}` does not exist")
    quiet = {"n/a", "none"}
    if not paths and not (stage0 in quiet and stage1 in quiet):
        errors.append(f"{where}: no evidence")
    open_gap = stage0 in ("partial", "none") or stage1 in ("partial", "none")
    if gaps == "-":
        if open_gap:
            errors.append(f"{where}: a partial or missing stage needs a gap")
        return
    for gap in gaps.split(";"):
        if not gap.strip().startswith(CLASSES):
            errors.append(f"{where}: gap `{gap.strip()}` has no class {CLASSES}")


def main():
    errors = []
    rows = []
    for no, line in enumerate(MATRIX.read_text(encoding="utf-8").splitlines(), 1):
        if not ROW_RE.match(line):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 5:
            errors.append(f"{MATRIX.relative_to(ROOT)}:{no}: {len(cells)} cells, not 5")
            continue
        rows.append(cells[0])
        check_row(no, cells, errors)
    sections = spec_sections()
    if rows != sections:
        missing = [s for s in sections if s not in rows]
        extra = [r for r in rows if r not in sections]
        for section in missing:
            errors.append(f"conformance: no row for spec section `{section}`")
        for row in extra:
            errors.append(f"conformance: row `{row}` is not a spec section")
        if not missing and not extra:
            errors.append("conformance: the rows are not in the order of the spec")
    for error in errors:
        print(error)
    if errors:
        return 1
    print(f"conformance: {len(rows)} sections checked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
