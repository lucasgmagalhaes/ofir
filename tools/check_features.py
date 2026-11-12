#!/usr/bin/env python3
"""Checks the feature state registry (docs/design/feature-states.md, ADR 0038).

- the `## Planned`, `## Open` and `## Rejected` tables have rows, and every
  link in them names a file that exists;
- each chapter of docs/spec has as many **[planned]** markers as Planned
  rows that link to it, and as many **[open]** markers as Open rows;
- every ADR has one row in docs/adr/README.md, and the status there starts
  with the same word as the record's `- Status:` line (Proposed, Accepted,
  Rejected or Superseded).
"""

import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs" / "design" / "feature-states.md"
SPEC = ROOT / "docs" / "spec"
ADRS = ROOT / "docs" / "adr"
STATES = {"Planned": "[planned]", "Open": "[open]", "Rejected": None}
STATUSES = {"Proposed", "Accepted", "Rejected", "Superseded"}
LINK_RE = re.compile(r"\]\(([^)#]+)(#[^)]*)?\)")
INDEX_RE = re.compile(r"^\| \[(\d{4})\]\(([^)]+)\) \| .* \| (\w+)[^|]* \|$")
STATUS_RE = re.compile(r"^- Status: (\w+)", re.MULTILINE)


def registry_rows():
    """The table rows of each state section, as (line number, line)."""
    rows = {state: [] for state in STATES}
    state = None
    for number, line in enumerate(REGISTRY.read_text(encoding="utf-8").splitlines(), 1):
        if line.startswith("## "):
            state = line[3:].strip() if line[3:].strip() in STATES else None
        elif state and line.startswith("| ") and not line.startswith("|--"):
            if not re.match(r"^\| (Feature|Question) \|", line):
                rows[state].append((number, line))
    return rows


def check_registry(errors):
    name = REGISTRY.relative_to(ROOT)
    rows = registry_rows()
    cited = {state: Counter() for state in STATES}
    for state, lines in rows.items():
        if not lines:
            errors.append(f"{name}: no rows under `## {state}`")
        for number, line in lines:
            for target, _ in LINK_RE.findall(line):
                path = (REGISTRY.parent / target).resolve()
                if not path.exists():
                    errors.append(f"{name}:{number}: `{target}` does not exist")
                elif path.parent == SPEC:
                    cited[state][path.name] += 1
    for chapter in sorted(SPEC.glob("[0-9][0-9]-*.md")):
        text = chapter.read_text(encoding="utf-8")
        for state, marker in STATES.items():
            if marker is None:
                continue
            marked = text.count(f"**{marker}**")
            if marked != cited[state][chapter.name]:
                errors.append(f"{name}: docs/spec/{chapter.name} has {marked} {marker} "
                              f"markers but {cited[state][chapter.name]} {state} rows")


def check_adrs(errors):
    index = ADRS / "README.md"
    listed = {}
    for line in index.read_text(encoding="utf-8").splitlines():
        match = INDEX_RE.match(line)
        if match:
            number, target, status = match.groups()
            if number in listed:
                errors.append(f"{index.relative_to(ROOT)}: ADR {number} is listed twice")
            listed[number] = (target, status)
    for path in sorted(ADRS.glob("[0-9][0-9][0-9][0-9]-*.md")):
        number = path.name[:4]
        match = STATUS_RE.search(path.read_text(encoding="utf-8"))
        status = match.group(1) if match else None
        where = path.relative_to(ROOT)
        if status not in STATUSES:
            errors.append(f"{where}: the status must start with one of "
                          f"{', '.join(sorted(STATUSES))}")
        if number not in listed:
            errors.append(f"{where}: not listed in {index.relative_to(ROOT)}")
            continue
        target, indexed = listed.pop(number)
        if target != path.name:
            errors.append(f"{index.relative_to(ROOT)}: ADR {number} links `{target}`")
        if status and indexed != status:
            errors.append(f"{index.relative_to(ROOT)}: ADR {number} is `{indexed}` "
                          f"there but `{status}` in {path.name}")
    for number in listed:
        errors.append(f"{index.relative_to(ROOT)}: ADR {number} has no record")


def main() -> int:
    errors = []
    check_registry(errors)
    check_adrs(errors)
    for error in errors:
        print(f"error: {error}", file=sys.stderr)
    if errors:
        return 1
    rows = registry_rows()
    print("feature states: " + ", ".join(f"{len(lines)} {state.lower()}"
                                         for state, lines in rows.items())
          + f", {len(list(ADRS.glob('[0-9][0-9][0-9][0-9]-*.md')))} ADRs indexed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
