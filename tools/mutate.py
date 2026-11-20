#!/usr/bin/env python3
"""Mutation testing of the compilers (roadmap 6.33k, ADR 0103).

Copies the tracked files to a work tree (build/mutation/tree), then mutates
the sources of `bootstrap/src` (stage 0) and `compiler/` (stage 1) one
change at a time: a comparison, `&&` and `||`, `+` and `-`, the constants
0 and 1, and `true` and `false`. For each mutant the compiler it belongs
to is rebuilt and the tests run with it (for stage 0 also
`tools/check_stage0_cli.py`; with `--compare-stages`, `tools/compare_stages.py`
too, which takes minutes per mutant): a test that fails kills the
mutant; a mutant that passes every test survives, and points to a missing
test (or is equivalent to the original). A mutant that does not build is
not counted. The mutants are sampled per directory (`--per-dir`, with
`--seed`), and the report gives, for each directory, the mutants killed
and the survivors with their file and line:

    python3 tools/mutate.py [--dirs bootstrap/src/sema,compiler/syntax] [--per-dir 10]
        [--seed 1] [--tests FILTER ...] [--jobs N] [--compare-stages] [--out build/mutation]

The report is written to <out>/report.md and <out>/report.json.
"""

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {"bootstrap/src": (".c", ".h"), "compiler": (".of",)}
# Each operator with spaces around it, so that `List<int>`, `->` and `*p`
# are left alone; the replacement keeps the spaces.
SWAPS = {" == ": " != ", " != ": " == ", " < ": " >= ", " >= ": " < ", " > ": " <= ",
         " <= ": " > ", " && ": " || ", " || ": " && ", " + ": " - ", " - ": " + "}
WORDS = {"true": "false", "false": "true", "0": "1", "1": "0"}
WORD = re.compile(r"(?<![\w.])(true|false|0|1)(?![\w.])")
STRING = re.compile(r'"(?:[^"\\]|\\.)*"' + r"|'(?:[^'\\]|\\.)*'")


@dataclass(frozen=True)
class Mutant:
    path: str      # relative to the root
    line: int      # 1-based
    column: int    # 0-based
    old: str
    new: str

    @property
    def directory(self) -> str:
        return str(Path(self.path).parent)

    def describe(self) -> str:
        return f"{self.path}:{self.line}: `{self.old.strip()}` -> `{self.new.strip()}`"


def code_part(line: str) -> str:
    """The line with its string literals blanked and its comment cut."""
    line = STRING.sub(lambda m: " " * len(m.group()), line)
    cut = line.find("//")
    return line if cut < 0 else line[:cut]


def mutants_of(path: Path, rel: str) -> list[Mutant]:
    found = []
    in_comment = False
    for number, text in enumerate(path.read_text().splitlines(), 1):
        stripped = text.strip()
        if in_comment:
            in_comment = "*/" not in stripped
            continue
        if stripped.startswith("/*"):
            in_comment = "*/" not in stripped
            continue
        if stripped.startswith(("#", "*", "//", "import ")):
            continue
        code = code_part(text)
        for old, new in SWAPS.items():
            start = code.find(old)
            while start >= 0:
                found.append(Mutant(rel, number, start, old, new))
                start = code.find(old, start + 1)
        for m in WORD.finditer(code):
            found.append(Mutant(rel, number, m.start(), m.group(), WORDS[m.group()]))
    return found


def all_mutants(dirs: list[str]) -> dict[str, list[Mutant]]:
    by_dir: dict[str, list[Mutant]] = defaultdict(list)
    for top, suffixes in SOURCES.items():
        for path in sorted((ROOT / top).rglob("*")):
            rel = str(path.relative_to(ROOT))
            if path.suffix not in suffixes or "/fuzz/" in rel:
                continue
            if dirs and not any(rel.startswith(d.rstrip("/") + "/") for d in dirs):
                continue
            for m in mutants_of(path, rel):
                by_dir[m.directory].append(m)
    return by_dir


def copy_tree(tree: Path):
    files = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True,
                           check=True).stdout.splitlines()
    for rel in files:
        source = ROOT / rel
        target = tree / rel
        if not source.is_file():
            continue
        if target.exists() and target.read_bytes() == source.read_bytes():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


class Runner:
    def __init__(self, tree: Path, tests: list[str], jobs: int, timeout: int, compare: bool):
        self.tree = tree
        self.compare = compare
        self.tests = tests
        self.jobs = jobs
        self.timeout = timeout
        self.env = dict(os.environ, OFIR_STD=str(tree / "std"),
                        OFIR_RUNTIME=str(tree / "build" / "libofir_rt.a"))

    def run(self, args: list[str]) -> bool:
        try:
            done = subprocess.run(args, cwd=self.tree, env=self.env, timeout=self.timeout,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            return False
        return done.returncode == 0

    def build(self, stage: int) -> bool:
        return self.run(["make", "-s", "runtime", "stage1" if stage else "stage0"])

    def test(self, stage: int) -> bool:
        """The tests with the compiler of `stage`; for stage 0 also its command
        line checks, and with `--compare-stages` the output of both stages."""
        build = self.tree / "build"
        compiler = build / f"stage{stage}" / "ofir"
        if not self.run([sys.executable, "tests/runner/run_tests.py", "--compiler", str(compiler),
                         "--stage", str(stage), "-j", str(self.jobs), "--no-retry", *self.tests]):
            return False
        if stage == 0 and not self.run([sys.executable, "tools/check_stage0_cli.py", "--compiler",
                                        str(compiler), "--runtime", str(build / "libofir_rt.a")]):
            return False
        return not self.compare or self.run([
            sys.executable, "tools/compare_stages.py", "--compiler", str(build / "stage0" / "ofir"),
            "--stage1", str(build / "stage1" / "ofir")])


def stage_of(m: Mutant) -> int:
    return 0 if m.path.startswith("bootstrap/") else 1


def apply(tree: Path, m: Mutant, original: str) -> str:
    lines = original.split("\n")
    line = lines[m.line - 1]
    lines[m.line - 1] = line[:m.column] + m.new + line[m.column + len(m.old):]
    return "\n".join(lines)


def write_report(out: Path, results: dict[str, dict]):
    rows = ["# Mutation testing", "",
            "| Directory | Mutants | Killed | Survived | Not built | Score |",
            "| --- | --- | --- | --- | --- | --- |"]
    for d, r in sorted(results.items()):
        counted = r["killed"] + len(r["survived"])
        score = f"{100.0 * r['killed'] / counted:.0f}%" if counted else "-"
        rows.append(f"| {d} | {counted + r['invalid']} | {r['killed']} | {len(r['survived'])} "
                    f"| {r['invalid']} | {score} |")
    rows += ["", "## Survivors", ""]
    for d, r in sorted(results.items()):
        rows += [f"- {s}" for s in r["survived"]]
    (out / "report.md").write_text("\n".join(rows) + "\n")
    (out / "report.json").write_text(json.dumps(results, indent=2) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dirs", default="", help="comma-separated directories to mutate")
    parser.add_argument("--per-dir", type=int, default=10, help="mutants sampled per directory")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--tests", nargs="*", default=[], help="filters for run_tests.py")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--timeout", type=int, default=1800, help="seconds for a build or a run")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "mutation")
    parser.add_argument("--compare-stages", action="store_true",
                        help="also compare the output of the stages on each mutant (slow)")
    parser.add_argument("--list", action="store_true", help="count the mutants and stop")
    opts = parser.parse_args()
    by_dir = all_mutants([d for d in opts.dirs.split(",") if d])
    if opts.list:
        for d, ms in sorted(by_dir.items()):
            print(f"{d}: {len(ms)} mutants")
        return 0
    rng = random.Random(opts.seed)
    sample = {d: rng.sample(ms, min(opts.per_dir, len(ms))) for d, ms in sorted(by_dir.items())}
    out = opts.out.resolve()
    tree = out / "tree"
    copy_tree(tree)
    runner = Runner(tree, opts.tests, opts.jobs, opts.timeout, opts.compare_stages)
    for stage in sorted({stage_of(m) for ms in sample.values() for m in ms}):
        if not (runner.build(stage) and runner.test(stage)):
            print(f"mutate: the tests fail on the unmutated stage {stage}; nothing to measure")
            return 1
    results = {}
    total = sum(len(ms) for ms in sample.values())
    done = 0
    for d, ms in sample.items():
        r = {"killed": 0, "survived": [], "invalid": 0}
        for m in ms:
            done += 1
            target = tree / m.path
            original = target.read_text()
            target.write_text(apply(tree, m, original))
            try:
                if not runner.build(stage_of(m)):
                    r["invalid"] += 1
                    verdict = "not built"
                elif runner.test(stage_of(m)):
                    r["survived"].append(m.describe())
                    verdict = "SURVIVED"
                else:
                    r["killed"] += 1
                    verdict = "killed"
            finally:
                target.write_text(original)
            print(f"[{done}/{total}] {verdict}: {m.describe()}", flush=True)
        results[d] = r
        write_report(out, results)
    survived = sum(len(r["survived"]) for r in results.values())
    killed = sum(r["killed"] for r in results.values())
    print(f"mutate: {killed} killed, {survived} survived; report {out / 'report.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
