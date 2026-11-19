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

Cases (ADR 0085): an evidence path under `tests/compile_fail/` or
`tests/panic/`, or a file under `tests/borrow/` with an `// error[`
annotation, is a negative case; any other path under `tests/` is a
positive one, and a directory is each kind its `.of` files are. A section
a stage implements needs both kinds, or the gap `untested: no positive
case` or `untested: no negative case`. A section stage 0 implements
needs a case without `// requires: stage1`, or `untested: no stage 0
case`. A gap that names a missing case is an error once the case exists.
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


NEGATIVE_DIRS = ("tests/compile_fail/", "tests/panic/")
CASE_GAPS = {
    "positive": "untested: no positive case",
    "negative": "untested: no negative case",
    "stage0": "untested: no stage 0 case",
}


def case_files(path):
    full = ROOT / path
    if full.is_dir():
        return sorted(p for p in full.rglob("*.of"))
    return [full]


def case_kinds(paths):
    """The kinds of case (`positive`, `negative`, `stage0`) the paths give."""
    kinds = set()
    for path in paths:
        if not path.startswith("tests/") or not (ROOT / path).exists():
            continue
        for file in case_files(path):
            rel = file.relative_to(ROOT).as_posix()
            text = file.read_text(encoding="utf-8", errors="replace")
            negative = rel.startswith(NEGATIVE_DIRS) or (
                rel.startswith("tests/borrow/") and "// error[" in text
            )
            kinds.add("negative" if negative else "positive")
            head = [line for line in text.splitlines()[:10] if line.startswith("// requires:")]
            if file.suffix == ".of" and not any("stage1" in line for line in head):
                kinds.add("stage0")
    return kinds


def check_cases(where, stage0, stage1, paths, gaps, errors):
    implemented = {"full", "partial"}
    needed = set()
    if stage0 in implemented or stage1 in implemented:
        needed |= {"positive", "negative"}
    if stage0 in implemented:
        needed.add("stage0")
    kinds = case_kinds(paths)
    for kind, gap in CASE_GAPS.items():
        if kind in needed and kind not in kinds and gap not in gaps:
            errors.append(f"{where}: no {kind} case: add one, or the gap `{gap}`")
        if kind in kinds and gap in gaps:
            errors.append(f"{where}: gap `{gap}`, yet the evidence has one")


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
    check_cases(where, stage0, stage1, paths, gaps, errors)
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
