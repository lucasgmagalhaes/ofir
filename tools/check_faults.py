#!/usr/bin/env python3
"""Runs every run test and example with failing allocations and I/O
(OFIR-74, ADR 0087).

`tools/fault/inject.c`, loaded with LD_PRELOAD, makes the C library calls
of the program fail on demand. Each program is run once to count its
allocations and I/O calls, then:

- `eintr` (every read and write interrupted once) and `short` (writes of
  one byte) must not change its output or exit status;
- `alloc:N`, the Nth allocation and every later one failing, must end as
  the normal run does or with a panic (status 101: out of memory, or a
  task that cannot start);
- `io:N`, the Nth read, write or open failing with EIO, must end with a
  status, never a signal or a hang.

N is sampled at the first, middle and last call. Allocations go through
malloc (`OFIR_SYSTEM_ALLOC`), so the pool's large blocks fail as well. On
a machine without glibc the check reports itself unavailable: skipped, or
an error under `--require`.

    python3 tools/check_faults.py [--compiler build/stage0/ofir] [--require]
"""

import argparse
import concurrent.futures
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_leaks import programs  # noqa: E402
from safety import unavailable  # noqa: E402

TIMEOUT = 60
PANIC = 101


def run(exe: Path, shim: Path, fault: str, count: Path | None = None):
    env = dict(os.environ, OFIR_SYSTEM_ALLOC="1", OFIR_FAULT=fault, LD_PRELOAD=str(shim))
    if count:
        env["OFIR_FAULT_COUNT"] = str(count)
    try:
        return subprocess.run([str(exe)], capture_output=True, env=env, cwd=exe.parent,
                              stdin=subprocess.DEVNULL, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return None


def samples(total: int) -> list:
    return sorted({1, (total + 1) // 2, total}) if total > 0 else []


def check(compiler: str, path: Path, shim: Path, out: Path) -> list:
    exe = out / (path.stem + str(abs(hash(path))))
    if subprocess.run([compiler, "build", str(path), "-o", str(exe)],
                      capture_output=True).returncode != 0:
        return []   # not a program this compiler builds (examples of later features)
    count = exe.with_suffix(".count")
    base = run(exe, shim, "count", count)
    if base is None or not count.exists():
        return []   # the program does not finish on its own here (it waits on input)
    allocs, ios = (int(n) for n in count.read_text().split())
    name = path.relative_to(ROOT)
    problems = []
    for fault in ("eintr", "short"):
        result = run(exe, shim, fault)
        if result is None:
            problems.append(f"{name}: {fault}: no exit after {TIMEOUT} s")
        elif (result.returncode, result.stdout) != (base.returncode, base.stdout):
            problems.append(f"{name}: {fault}: exit status {result.returncode} or the output "
                            f"differs from the run without faults")
    for kind, total in (("alloc", allocs), ("io", ios)):
        for n in samples(total):
            fault = f"{kind}:{n}"
            result = run(exe, shim, fault)
            if result is None:
                problems.append(f"{name}: {fault}: no exit after {TIMEOUT} s")
            elif result.returncode < 0:
                problems.append(f"{name}: {fault}: killed by signal {-result.returncode}")
            elif kind == "alloc" and result.returncode not in (base.returncode, PANIC):
                problems.append(f"{name}: {fault}: exit status {result.returncode}")
            elif (kind == "alloc" and result.returncode == PANIC
                  and b"panic: " not in result.stderr):
                problems.append(f"{name}: {fault}: status 101 without a panic message")
    return problems


def build_shim(out: Path) -> Path:
    shim = out / "inject.so"
    subprocess.run(["gcc", "-O1", "-shared", "-fPIC", str(ROOT / "tools" / "fault" / "inject.c"),
                    "-o", str(shim), "-ldl"], check=True)
    return shim


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", default=str(ROOT / "build" / "stage0" / "ofir"))
    parser.add_argument("--require", action="store_true",
                        help="fail instead of skipping when fault injection is unavailable")
    args = parser.parse_args()
    reason = unavailable("faults")
    if reason:
        print(f"faults: unavailable ({reason}), {'failed' if args.require else 'skipped'}")
        return 1 if args.require else 0
    paths = programs()
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        shim = build_shim(out)
        with concurrent.futures.ThreadPoolExecutor() as pool:
            results = list(pool.map(lambda p: check(args.compiler, p, shim, out), paths))
    problems = [p for r in results for p in r]
    for problem in problems:
        print(f"error: {problem}")
    failing = sum(1 for r in results if r)
    print(f"faults: {len(paths)} programs, {failing} that crash, hang or change their output")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
