#!/usr/bin/env python3
"""Which memory and concurrency checks this machine can run (OFIR-74,
ADR 0087). Each check probes its tools by building and running a small C
program, so a check that cannot run says why instead of passing silently.

    python3 tools/safety.py      # prints one line per check
"""

import functools
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROBE = "#include <stdlib.h>\nint main(void) { free(malloc(8)); return 0; }\n"
# check -> (what it is, the make target that runs it)
CHECKS = {
    "leaks": ("valgrind: leaks and invalid accesses", "check-leaks"),
    "asan": ("AddressSanitizer and UBSan", "check-asan"),
    "tsan": ("ThreadSanitizer: data races", "check-tsan"),
    "faults": ("failing allocations and I/O (LD_PRELOAD)", "check-faults"),
}


def probe(flags: list) -> str | None:
    """None when gcc builds and runs PROBE with `flags`, else the reason."""
    if not shutil.which("gcc"):
        return "gcc is not installed"
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp) / "probe.c", Path(tmp) / "probe"
        src.write_text(PROBE)
        built = subprocess.run(["gcc", *flags, str(src), "-o", str(exe)],
                               capture_output=True, text=True)
        if built.returncode != 0:
            return f"gcc {' '.join(flags)} does not link ({built.stderr.strip()[:80]})"
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        if ran.returncode != 0:
            return f"a {' '.join(flags)} program does not run ({ran.stderr.strip()[:80]})"
    return None


def glibc() -> bool:
    try:
        return os.confstr("CS_GNU_LIBC_VERSION") is not None
    except (ValueError, OSError, AttributeError):
        return False


@functools.cache
def unavailable(check: str) -> str | None:
    """None when `check` can run here, else why it cannot."""
    if check == "leaks":
        return None if shutil.which("valgrind") else "valgrind is not installed"
    if check == "asan":
        return probe(["-fsanitize=address,undefined"])
    if check == "tsan":
        return probe(["-fsanitize=thread"])
    if check == "faults":
        if not sys.platform.startswith("linux") or not glibc():
            return "the fault injector interposes glibc with LD_PRELOAD: Linux with glibc only"
        return probe([])
    raise ValueError(check)


def main() -> int:
    for check, (what, target) in CHECKS.items():
        reason = unavailable(check)
        state = "available" if reason is None else f"unavailable: {reason}"
        print(f"{target:13} {what:44} {state}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
