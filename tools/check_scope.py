#!/usr/bin/env python3
"""Checks the scope and capacity page (docs/design/scope.md, ADR 0102,
OFIR-86).

- Open work: one row per open roadmap row, and no other. A roadmap row is
  open when its last cell is empty, `pending` or `in progress`; an API row
  (`API-NN to API-NN`) is listed while its group is open in the API plan.
  No listed row is **done** in the roadmap.
- Each row has a priority G0, G1 or G2, an owner of the maintainers table
  (docs/maintaining.md) and a state `in progress`, `next` or `queued`.
  Its dependencies are roadmap rows, without a cycle; a row that is
  `in progress` or `next` depends on no row that is still open.
- Capacity: an owner has at most one row in progress and two next.
- Next pull requests: each row in progress or next has one or more, each
  of size S or M, and no other row has any.
- Rejected and deferred: each decision is `rejected...` or `deferred` and
  links a file that exists.

    python3 tools/check_scope.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCOPE = ROOT / "docs" / "design" / "scope.md"
ROADMAP = ROOT / "docs" / "roadmap.md"
MAINTAINING = ROOT / "docs" / "maintaining.md"

ROW = re.compile(r"^\| (\d+\.\d+[a-z]?) \|")
API = re.compile(r"\| API-\d\d to API-\d\d \|")
LINK = re.compile(r"\]\(([^)#]+)(#[^)]*)?\)")
STATES = ("in progress", "next", "queued")
ACTIVE = ("in progress", "next")


def section(text, heading):
    """The lines of a `## heading` section."""
    out, inside = [], False
    for line in text.splitlines():
        if line.startswith("## "):
            inside = line == f"## {heading}"
            continue
        if inside:
            out.append(line)
    return out


def rows(lines):
    """The cells of each body row of the first table of the lines."""
    table = [line for line in lines if line.startswith("|")]
    return [[cell.strip() for cell in row.strip("|").split("|")] for row in table[2:]]


def roadmap():
    """{row: (last cell, whether it is an API group)}."""
    found = {}
    for line in ROADMAP.read_text().splitlines():
        match = ROW.match(line)
        if match:
            last = line.rstrip().rstrip("|").rsplit("|", 1)[-1].strip()
            found.setdefault(match.group(1), (last, bool(API.search(line))))
    return found


def is_open(last):
    plain = last.replace("*", "").strip()
    return plain in ("", "pending") or plain.startswith("in progress")


def check_open(work, rows_of_roadmap, errors):
    for row, (last, api) in rows_of_roadmap.items():
        if (is_open(last) or (api and "**done**" not in last)) and row not in work:
            errors.append(f"roadmap row {row} is open but not in Open work")
    for row in work:
        if row not in rows_of_roadmap:
            errors.append(f"{row} is not a roadmap row")
        elif "**done**" in rows_of_roadmap[row][0]:
            errors.append(f"{row} is done in the roadmap but still in Open work")


def check_rows(work, owners, rows_of_roadmap, errors):
    for row, (priority, owner, deps, state) in work.items():
        if priority not in ("G0", "G1", "G2"):
            errors.append(f"{row}: priority `{priority}` is not G0, G1 or G2")
        if owner not in owners:
            errors.append(f"{row}: `{owner}` is not in the maintainers table")
        if state not in STATES:
            errors.append(f"{row}: state `{state}` is not one of {', '.join(STATES)}")
        for dep in deps:
            if dep not in rows_of_roadmap:
                errors.append(f"{row}: dependency {dep} is not a roadmap row")
            elif state in ACTIVE and dep in work:
                errors.append(f"{row} is {state} but depends on the open row {dep}")
    for start in work:
        seen, stack = set(), list(work[start][2])
        while stack:
            dep = stack.pop()
            if dep == start:
                errors.append(f"{start} depends on itself")
                break
            if dep in work and dep not in seen:
                seen.add(dep)
                stack.extend(work[dep][2])


def check_capacity(work, owners, errors):
    for owner in owners:
        states = [w[3] for w in work.values() if w[1] == owner]
        if states.count("in progress") > 1:
            errors.append(f"{owner} has {states.count('in progress')} rows in progress")
        if states.count("next") > 2:
            errors.append(f"{owner} has {states.count('next')} rows next")


def check_pulls(text, work, errors):
    estimated = set()
    for row, title, size in rows(section(text, "Next pull requests")):
        estimated.add(row)
        if row not in work or work[row][3] not in ACTIVE:
            errors.append(f"`{title}` estimates {row}, which is not in progress or next")
        if size not in ("S", "M"):
            errors.append(f"`{title}` has size `{size}`: split it into S or M")
    for row, (_, _, _, state) in work.items():
        if state in ACTIVE and row not in estimated:
            errors.append(f"{row} is {state} but has no estimated pull request")


def check_decisions(text, errors):
    table = rows(section(text, "Rejected and deferred"))
    if not table:
        errors.append("Rejected and deferred has no decision")
    for alternative, outcome, decided, _ in table:
        if not (outcome.startswith("rejected") or outcome == "deferred"):
            errors.append(f"{alternative}: outcome `{outcome}`")
        links = LINK.findall(decided)
        if not links:
            errors.append(f"{alternative}: no link to its decision")
        for target, _ in links:
            if not (SCOPE.parent / target).exists():
                errors.append(f"{alternative}: {target} does not exist")


def main():
    errors = []
    text = SCOPE.read_text()
    owners = {r[0] for r in rows(section(MAINTAINING.read_text(), "Maintainers"))}
    work = {}
    for row, priority, owner, deps, state in rows(section(text, "Open work")):
        if row in work:
            errors.append(f"{row} is in Open work twice")
        listed = [] if deps == "-" else [d.strip() for d in deps.split(",")]
        work[row] = (priority, owner, listed, state)
    rows_of_roadmap = roadmap()
    check_open(work, rows_of_roadmap, errors)
    check_rows(work, owners, rows_of_roadmap, errors)
    check_capacity(work, owners, errors)
    check_pulls(text, work, errors)
    check_decisions(text, errors)
    for error in errors:
        print(f"scope: {error}", file=sys.stderr)
    print(f"scope: {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
