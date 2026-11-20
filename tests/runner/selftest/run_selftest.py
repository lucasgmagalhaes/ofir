#!/usr/bin/env python3
"""Self-test of the test runner, using the fake compiler."""

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUNNER = HERE.parent / "run_tests.py"
FAKE = HERE / "fake_ofir.py"


def runner(tests_dir, stage, *flags):
    for tried in HERE.rglob("*.tried"):
        tried.unlink()
    return subprocess.run(
        [sys.executable, str(RUNNER), "--compiler", str(FAKE), "--stage", str(stage),
         "--tests-dir", str(HERE / tests_dir), "-j", "4",
         "--flaky-log", str(Path(tempfile.gettempdir()) / "ofir-selftest-flaky.txt"), *flags],
        capture_output=True, text=True)


def expect(name, proc, exit_code, summary):
    ok = proc.returncode == exit_code and summary in proc.stdout
    print(f"{'ok  ' if ok else 'FAIL'} {name}")
    if not ok:
        print(proc.stdout, proc.stderr)
    return ok


def main():
    fail_count = len(list((HERE / "fail").rglob("*.of")))
    pass_count = len(list((HERE / "pass").rglob("*.of")))
    results = [
        expect("passing fixtures (stage 1)", runner("pass", 1), 0,
               f"{pass_count} tests: {pass_count} passed, 0 failed"),
        expect("passing fixtures (stage 0)", runner("pass", 0), 0, "0 failed"),
        expect("stage 0 rules", runner("pass_stage0", 0), 0, "1 passed, 0 failed, 1 skipped"),
        expect("stage 1 rejects stage 0 leniency", runner("pass_stage0", 1), 1, "1 failed"),
        expect("failing fixtures", runner("fail", 1), 1,
               f"{fail_count} tests: 0 passed, {fail_count} failed"),
        expect("a flaky test is run again and reported", runner("flaky", 1), 0,
               "1 tests: 0 passed, 0 failed, 0 skipped, 1 flaky"),
        expect("--no-retry fails a flaky test", runner("flaky", 1, "--no-retry"), 1,
               "1 tests: 0 passed, 1 failed"),
    ]
    for leftover in [*HERE.rglob("*.actual"), *HERE.rglob("*.tried")]:
        leftover.unlink()
    if not all(results):
        return 1
    print("runner self-test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
