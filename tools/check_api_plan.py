#!/usr/bin/env python3
"""Checks the API ecosystem plan (docs/design/api-ecosystem.md, ADR 0094,
OFIR-79).

- Records: the backlog tables hold API-01 to API-NN once each, in order,
  under `### ... — roadmap 6.N` headings whose ranges are the ones of the
  roadmap's `| 6.N | ... | API-A to API-B |` rows.
- Dependencies: a dependency that is only an API ID is a hard one. It
  names an existing record other than its own, in the same gate or an
  earlier one, and the hard dependencies have no cycle.
- States: `## Record states` has one row per record, in order. `pending`
  has no evidence; `partial` cites files and says what is missing; `done`
  cites a test and misses nothing. Every cited path exists.
- Gates: `## Delivery gates` lists API-F, API-L, API-P and API-X. Each
  record is in one gate, or in two when its priority names a local and a
  production variant. A P0 record is in API-F or API-L, a P2 record in
  API-X. A gate is `reached` exactly when it and the gate it builds on
  have every record done, and `open` otherwise; API-X has state `-`.

    python3 tools/check_api_plan.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLAN = "docs/design/api-ecosystem.md"
ROADMAP = "docs/roadmap.md"

GATES = ["API-F", "API-L", "API-P", "API-X"]
SECTION = re.compile(r"^### .* — roadmap (6\.\d+a?)$")
RECORD = re.compile(r"^\| API-(\d\d) \| ([^|]+)\|[^|]+\|([^|]+)\|$")
RANGE = re.compile(r"^\| (6\.\d+a?) \|.*\| API-(\d\d) to API-(\d\d) \|")
STATE = re.compile(r"^\| API-(\d\d) \| (\w+) \| ([^|]+)\| ([^|]+)\|$")
GATE = re.compile(r"^\| (API-[FLPX]) \| ([^|]+)\| ([^|]+)\| ([^|]+)\|$")
HARD = re.compile(r"^API-(\d\d)$")


def section(lines, title):
    """The lines under `## title`, up to the next `## ` heading."""
    out, inside = [], False
    for line in lines:
        if line.startswith("## "):
            inside = line[3:].strip() == title
            continue
        if inside:
            out.append(line)
    return out


def records(lines, errors):
    """{number: (priority, [hard dependencies], roadmap section)}."""
    found, current = {}, None
    for line in section(lines, "Backlog"):
        heading = SECTION.match(line)
        if heading:
            current = heading.group(1)
            continue
        match = RECORD.match(line)
        if not match:
            continue
        number = int(match.group(1))
        if number in found:
            errors.append(f"API-{number:02}: listed twice")
        hard = []
        for part in re.split(r"[;,]", match.group(3)):
            dep = HARD.match(part.strip())
            if dep:
                hard.append(int(dep.group(1)))
        found[number] = (match.group(2).strip(), hard, current)
    if list(found) != list(range(1, len(found) + 1)):
        errors.append(f"records are not API-01 to API-{len(found):02} in order")
    return found


def check_ranges(found, errors):
    ranges = {}
    for line in (ROOT / ROADMAP).read_text().splitlines():
        match = RANGE.match(line)
        if match:
            ranges[match.group(1)] = (int(match.group(2)), int(match.group(3)))
    sections = {}
    for number, (_, _, where) in found.items():
        sections.setdefault(where, []).append(number)
    for where, numbers in sections.items():
        if where not in ranges:
            errors.append(f"roadmap {where}: no `API-A to API-B` row in {ROADMAP}")
            continue
        low, high = ranges[where]
        if numbers != list(range(low, high + 1)):
            errors.append(f"roadmap {where}: the plan holds API-{numbers[0]:02} "
                          f"to API-{numbers[-1]:02}, the roadmap API-{low:02} to API-{high:02}")


def check_states(lines, found, errors):
    states, order = {}, []
    for line in section(lines, "Record states"):
        match = STATE.match(line)
        if not match:
            continue
        number, state = int(match.group(1)), match.group(2)
        evidence, missing = match.group(3).strip(), match.group(4).strip()
        order.append(number)
        states[number] = state
        name = f"API-{number:02}"
        paths = re.findall(r"`([^`]+)`", evidence)
        for path in paths:
            if not (ROOT / path).exists():
                errors.append(f"{name}: `{path}` does not exist")
        if state == "pending":
            if evidence != "-" or missing != "-":
                errors.append(f"{name}: a pending record has no evidence and no Missing")
        elif state == "partial":
            if not paths:
                errors.append(f"{name}: a partial record cites its files")
            if missing == "-":
                errors.append(f"{name}: a partial record says what is missing")
        elif state == "done":
            if not any(p.startswith("tests/") or p.endswith("_test.of") for p in paths):
                errors.append(f"{name}: a done record cites a test")
            if missing != "-":
                errors.append(f"{name}: a done record misses nothing")
        else:
            errors.append(f"{name}: unknown state `{state}`")
    if order != list(found):
        errors.append("## Record states: not one row per record, in order")
    return states


def check_gates(lines, found, states, errors):
    gates, rows = {}, []
    for line in section(lines, "Delivery gates and recommended order"):
        match = GATE.match(line)
        if match:
            ids = match.group(3).split()
            numbers = [int(x[4:]) for x in ids if re.fullmatch(r"API-\d\d", x)]
            if len(numbers) != len(ids):
                errors.append(f"{match.group(1)}: records must be API IDs")
            gates[match.group(1)] = (match.group(2).strip(), numbers, match.group(4).strip())
            rows.append(match.group(1))
    if rows != GATES:
        errors.append(f"## Delivery gates: the table lists {rows}, not {GATES}")
        return
    where = {}
    for gate in GATES:
        for number in gates[gate][1]:
            if number not in found:
                errors.append(f"{gate}: API-{number:02} is not a record")
            where.setdefault(number, []).append(gate)
    for number, (priority, hard, _) in found.items():
        name = f"API-{number:02}"
        placed = where.get(number, [])
        twice = "local" in priority and "production" in priority
        if len(placed) != (2 if twice else 1):
            errors.append(f"{name}: in {len(placed)} gates")
            continue
        if priority.startswith("P0") and placed[0] not in ("API-F", "API-L"):
            errors.append(f"{name}: a P0 record is in {placed[0]}")
        if priority.startswith("P2") and placed[0] != "API-X":
            errors.append(f"{name}: a P2 record is in {placed[0]}")
        for dep in hard:
            if dep == number or dep not in found:
                errors.append(f"{name}: depends on API-{dep:02}, not another record")
            elif dep in where and GATES.index(where[dep][0]) > GATES.index(placed[0]):
                errors.append(f"{name} ({placed[0]}) depends on API-{dep:02} ({where[dep][0]})")
    reached = {}
    for gate in GATES:
        base, numbers, state = gates[gate]
        if gate == "API-X":
            if base != "-" or state != "-":
                errors.append("API-X builds on nothing and has no state")
            continue
        done = all(states.get(n) == "done" for n in numbers)
        if base != "-":
            done = done and reached.get(base, False)
        reached[gate] = done
        expected = "reached" if done else "open"
        if state != expected:
            errors.append(f"{gate}: state `{state}`, expected `{expected}`")


def check_cycles(found, errors):
    seen, active = set(), set()

    def visit(number, path):
        if number in active:
            cycle = path[path.index(number):] + [number]
            errors.append("dependency cycle: " + " -> ".join(f"API-{n:02}" for n in cycle))
            return
        if number in seen or number not in found:
            return
        active.add(number)
        for dep in found[number][1]:
            visit(dep, path + [number])
        active.discard(number)
        seen.add(number)

    for number in found:
        visit(number, [])


def main():
    lines = (ROOT / PLAN).read_text().splitlines()
    errors = []
    found = records(lines, errors)
    check_ranges(found, errors)
    check_cycles(found, errors)
    states = check_states(lines, found, errors)
    check_gates(lines, found, states, errors)
    for error in errors:
        print(f"{PLAN}: {error}", file=sys.stderr)
    counts = {s: list(states.values()).count(s) for s in ("done", "partial", "pending")}
    print(f"api plan: {len(found)} records ({counts['done']} done, "
          f"{counts['partial']} partial, {counts['pending']} pending), {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
