#!/usr/bin/env python3
"""Checks line coverage and profiling of an Ofir program (OFIR-61, ADR
0073, docs/design/toolchain.md).

Coverage: builds a small program with `--coverage`, runs it and reads the
counters with `llvm-cov gcov`: each line has the count of its executions,
the lines of a generic function add up the counts of all its instances,
a line that never ran is marked `#####`, and a second run adds to the
counts. Skipped when clang has no profile runtime or `llvm-cov` is
missing.

Profiling: runs the program built with `-g` under `valgrind
--tool=callgrind`, and `callgrind_annotate` names its functions (a
generic instance with its type arguments) and their source file. Skipped
when valgrind is not installed.

Both print the overhead they measure: the run time of the instrumented
build over the plain one.

    python3 tools/check_profile.py --compiler build/stage1/ofir
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PROGRAM = """\
fn twice<T>(value: move T, n: int) T {
    if n > 100 {
        print("big")
    }
    return value
}

fn classify(n: int) str {
    if n < 0 {
        return "neg"
    }
    return "pos"
}

fn main() {
    let total = 0
    for i in 0..10 {
        total += twice(i, i)
    }
    const s = twice("a", 1)
    print("{total} {s} {classify(3)}")
}
"""

# A loop long enough to time, for the overhead.
BUSY = """\
fn step(x: int, i: int) int {
    if i % 3 == 0 {
        return (x * 31 + i) % 1000003
    }
    return (x + i) % 1000003
}

fn main() {
    let x = 1
    for i in 0..30000000 {
        x = step(x, i)
    }
    print("{x}")
}
"""

# Line number to expected count after one run ("#####" never ran).
EXPECTED = {
    1: "11",      # twice: 10 calls of twice<int> and 1 of twice<str>
    2: "11",
    3: "#####",
    5: "11",
    9: "1",
    10: "#####",
    12: "1",
    17: "11",     # the loop test runs once more than its body
    18: "10",
    20: "1",
    21: "1",
}


def fail(message):
    print(f"profile: {message}", file=sys.stderr)
    sys.exit(1)


def build(compiler, source, exe, flags):
    result = subprocess.run([compiler, "build", str(source), "-o", str(exe), *flags],
                            capture_output=True, text=True, timeout=300)
    if result.returncode != 0:
        return result.stderr + result.stdout
    return None


def run(exe, cwd):
    start = time.perf_counter()
    result = subprocess.run([str(exe)], capture_output=True, text=True, cwd=cwd,
                            timeout=300)
    if result.returncode != 0:
        fail(f"{exe.name} exited with {result.returncode}: {result.stderr}")
    return time.perf_counter() - start, result.stdout


def llvm_cov():
    """The llvm-cov of the clang that compiles (`OFIR_CLANG`), whose gcov
    format matches, or else any."""
    clang = os.environ.get("OFIR_CLANG", "")
    if clang:
        path = Path(clang)
        sibling = path.with_name(path.name.replace("clang", "llvm-cov"))
        found = shutil.which(str(sibling))
        if found and sibling.name != path.name:
            return found
    for name in ["llvm-cov"] + [f"llvm-cov-{v}" for v in range(30, 13, -1)]:
        path = shutil.which(name)
        if path:
            return path
    return None


def line_counts(gcov_text):
    counts = {}
    for line in gcov_text.splitlines():
        match = re.match(r"\s*([^:]+):\s*(\d+):", line)
        if match and match.group(1).strip() != "-":
            counts[int(match.group(2))] = match.group(1).strip()
    return counts


def gcov(tool, work):
    data = sorted(str(p) for p in work.rglob("*.gcda"))
    if not data:
        fail("no .gcda file was written")
    result = subprocess.run([tool, "gcov", *data], capture_output=True, text=True,
                            cwd=work, timeout=120)
    if result.returncode != 0:
        fail(f"llvm-cov gcov failed: {result.stderr}")
    report = work / "main.of.gcov"
    if not report.exists():
        fail(f"no main.of.gcov: {result.stdout}")
    return line_counts(report.read_text())


def check_coverage(compiler, work):
    tool = llvm_cov()
    if tool is None:
        print("profile: coverage skipped, llvm-cov is not installed")
        return
    source = work / "main.of"
    source.write_text(PROGRAM)
    exe = work / "main"
    error = build(compiler, source, exe, ["--coverage"])
    if error is not None:
        if "profile" in error and ("cannot find" in error or "No such file" in error):
            print("profile: coverage skipped, clang has no profile runtime")
            return
        fail(f"build --coverage failed:\n{error}")
    _, out = run(exe, work)
    if out.strip() != "45 a pos":
        fail(f"unexpected output {out!r}")
    counts = gcov(tool, work)
    for line, want in EXPECTED.items():
        if counts.get(line) != want:
            fail(f"line {line}: count {counts.get(line)}, expected {want}")
    run(exe, work)   # the counters add up across runs
    counts = gcov(tool, work)
    if counts.get(18) != "20" or counts.get(3) != "#####":
        fail(f"after two runs line 18 is {counts.get(18)}, line 3 {counts.get(3)}")
    busy = work / "busy.of"
    busy.write_text(BUSY)
    plain, counted = work / "busy", work / "busy_cov"
    for exe, flags in [(plain, ["--release"]), (counted, ["--release", "--coverage"])]:
        error = build(compiler, busy, exe, flags)
        if error is not None:
            fail(f"build {flags} failed:\n{error}")
    base, _ = run(plain, work)
    cost, _ = run(counted, work)
    print(f"profile: coverage ok, overhead {cost / base:.2f}x at --release")


def check_callgrind(compiler, work):
    valgrind = shutil.which("valgrind")
    annotate = shutil.which("callgrind_annotate")
    if valgrind is None or annotate is None:
        print("profile: callgrind skipped, valgrind is not installed")
        return
    source = work / "prof.of"
    source.write_text(PROGRAM)
    exe = work / "prof"
    error = build(compiler, source, exe, ["-g"])
    if error is not None:
        fail(f"build -g failed:\n{error}")
    out = work / "callgrind.out"
    result = subprocess.run([valgrind, "--tool=callgrind", f"--callgrind-out-file={out}",
                             str(exe)], capture_output=True, text=True, cwd=work,
                            timeout=300)
    if result.returncode != 0:
        fail(f"callgrind failed: {result.stderr[-2000:]}")
    report = subprocess.run([annotate, "--inclusive=yes", "--threshold=100", str(out)],
                            capture_output=True, text=True, timeout=120).stdout
    for name in ["prof.of:ofir_main", "prof.of:ofir.twice<int>", "prof.of:ofir.classify"]:
        if name not in report:
            fail(f"callgrind_annotate does not name {name}:\n{report[-3000:]}")
    busy = work / "busy.of"
    busy.write_text(BUSY.replace("30000000", "3000000"))
    plain = work / "busy_g"
    error = build(compiler, busy, plain, ["--release", "-g"])
    if error is not None:
        fail(f"build --release -g failed:\n{error}")
    base, _ = run(plain, work)
    start = time.perf_counter()
    subprocess.run([valgrind, "--tool=callgrind", f"--callgrind-out-file={out}",
                    str(plain)], capture_output=True, cwd=work, timeout=600)
    cost = time.perf_counter() - start
    print(f"profile: callgrind ok, overhead {cost / base:.0f}x at --release -g")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    if sys.platform == "win32":
        print("profile: skipped on Windows (ADR 0073)")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        (work / "cov").mkdir()
        (work / "prof").mkdir()
        check_coverage(compiler, work / "cov")
        check_callgrind(compiler, work / "prof")
    return 0


if __name__ == "__main__":
    sys.exit(main())
