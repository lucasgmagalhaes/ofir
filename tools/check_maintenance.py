#!/usr/bin/env python3
"""Checks the maintenance process (docs/maintaining.md, ADR 0096, OFIR-81).

- SECURITY.md supports the minor of the compiler's version
  (`compiler/driver/version.of`), and no other.
- SECURITY.md and docs/maintaining.md give the same deadlines for a
  security report ("within N days", in order).
- Every label an issue template adds is in the triage table, and the
  template chooser links the private vulnerability report.
- `.github/CODEOWNERS` names exactly the maintainers of the table, and
  every holder of an account of `## Access` is a maintainer.
- No workflow reads a secret but `GITHUB_TOKEN`, as `## Access` says.

    python3 tools/check_maintenance.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAINTAINING = ROOT / "docs" / "maintaining.md"
SECURITY = ROOT / "SECURITY.md"
GITHUB = ROOT / ".github"
HANDLE = re.compile(r"@[A-Za-z0-9-]+")
DAYS = re.compile(r"within (\d+) days")


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


def check_versions(errors):
    match = re.search(r'VERSION: str = "(\d+)\.(\d+)\.\d+"',
                      (ROOT / "compiler/driver/version.of").read_text())
    minor = f"{match.group(1)}.{match.group(2)}.x"
    supported = [r[0] for r in rows(section(SECURITY.read_text(), "Supported versions"))
                 if r[1] == "yes"]
    if supported != [minor]:
        errors.append(f"SECURITY.md supports {supported}, the compiler is {minor}")


def check_deadlines(errors):
    report = DAYS.findall("\n".join(section(SECURITY.read_text(), "What happens next")))
    process = DAYS.findall("\n".join(section(MAINTAINING.read_text(), "Security")))
    if not report or report != process:
        errors.append(f"SECURITY.md gives the deadlines {report}, "
                      f"docs/maintaining.md#security {process}")


def check_labels(errors):
    text = MAINTAINING.read_text()
    known = {r[0].strip("`") for r in rows(section(text, "Triage"))}
    templates = sorted((GITHUB / "ISSUE_TEMPLATE").glob("*.yml"))
    for path in templates:
        if path.name == "config.yml":
            continue
        match = re.search(r"^labels: \[(.*)\]$", path.read_text(), re.M)
        labels = re.findall(r'"([^"]+)"', match.group(1)) if match else []
        if not labels:
            errors.append(f"{path.name} adds no label")
        for label in labels:
            if label not in known:
                errors.append(f"{path.name}: label `{label}` is not in the triage table")
    config = GITHUB / "ISSUE_TEMPLATE" / "config.yml"
    if not config.exists() or "/security/advisories/new" not in config.read_text():
        errors.append("ISSUE_TEMPLATE/config.yml does not link the vulnerability report")


def check_people(errors):
    text = MAINTAINING.read_text()
    maintainers = {r[0] for r in rows(section(text, "Maintainers"))}
    owners = set()
    for line in (GITHUB / "CODEOWNERS").read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            owners.update(HANDLE.findall(line))
    if owners != maintainers:
        errors.append(f"CODEOWNERS names {sorted(owners)}, the maintainers are "
                      f"{sorted(maintainers)}")
    for row in rows(section(text, "Access")):
        for holder in HANDLE.findall(row[-1]):
            if holder not in maintainers:
                errors.append(f"{row[0]} is held by {holder}, who is not a maintainer")


def check_secrets(errors):
    for path in sorted((GITHUB / "workflows").glob("*.yml")):
        for name in sorted(set(re.findall(r"secrets\.(\w+)", path.read_text()))):
            if name != "GITHUB_TOKEN":
                errors.append(f"{path.name} reads secrets.{name}; list it in "
                              "docs/maintaining.md#access and in this check")


def main():
    errors = []
    check_versions(errors)
    check_deadlines(errors)
    check_labels(errors)
    check_people(errors)
    check_secrets(errors)
    for error in errors:
        print(f"maintenance: {error}", file=sys.stderr)
    print(f"maintenance: {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
