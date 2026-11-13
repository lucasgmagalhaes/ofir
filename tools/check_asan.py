#!/usr/bin/env python3
"""Runs the programs of tests/run, tests/panic and examples/ with
AddressSanitizer and UndefinedBehaviorSanitizer (roadmap 3.12).

The generated code is instrumented by clang (`-fsanitize=address`) and the
C runtime is built with both sanitizers; gcc links them, since gcc ships
the sanitizer runtimes where clang may not. A run program must print its
`.out` file, a panic program must exit with status 101, and no program may
trigger a sanitizer report. The stack overflow tests are left out: the
sanitizer handles SIGSEGV itself.

    python3 tools/check_asan.py [--compiler build/stage0/ofir]
"""

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_leaks import programs  # noqa: E402

SANITIZE = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]


def build_runtime(out: Path) -> Path:
    objects = []
    for source in sorted((ROOT / "runtime" / "src").glob("*.c")):
        obj = out / (source.stem + ".o")
        subprocess.run(["gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O1", "-g",
                        "-I", str(ROOT / "runtime" / "include"), *SANITIZE, "-c", str(source),
                        "-o", str(obj)], check=True)
        objects.append(str(obj))
    library = out / "libofir_rt_asan.a"
    subprocess.run(["ar", "rcs", str(library), *objects], check=True)
    return library


def build(compiler: str, path: Path, runtime: Path, out: Path):
    base = out / (path.stem + str(abs(hash(path))))
    if subprocess.run([compiler, "build", str(path), "--emit=llvm", "-o", str(base)],
                      capture_output=True).returncode != 0:
        return None   # not a program stage 0 compiles (examples of later features)
    obj = base.with_suffix(".o")
    clang = os.environ.get("OFIR_CLANG") or "clang"
    subprocess.run([clang, "-c", "-O1", "-fsanitize=address", "-Wno-override-module",
                    str(base) + ".ll", "-o", str(obj)], check=True)
    subprocess.run(["gcc", *SANITIZE, str(obj), str(runtime), "-lm", "-o", str(base)],
                   check=True)
    return base


def check(compiler: str, path: Path, runtime: Path, out: Path):
    exe = build(compiler, path, runtime, out)
    if exe is None:
        return None
    # A panic runs no destructors (spec 6): what it leaves allocated is no leak.
    leaks = 0 if "panic" in path.parts else 1
    # A request no system can satisfy must reach the runtime's "out of memory"
    # panic as a null, not abort inside ASan (ADR 0048).
    asan = f"detect_leaks={leaks}:abort_on_error=0:allocator_may_return_null=1"
    env = dict(os.environ, ASAN_OPTIONS=asan,
               UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
    run = subprocess.run([str(exe)], capture_output=True, text=True, env=env,
                         stdin=subprocess.DEVNULL, timeout=300)
    name = path.relative_to(ROOT)
    # ASan warns about the request it refused; the panic that follows is the check.
    stderr = "\n".join(l for l in run.stderr.splitlines()
                       if "WARNING: AddressSanitizer failed to allocate" not in l)
    if "Sanitizer" in stderr or "runtime error:" in stderr:
        report = [l for l in stderr.splitlines() if "ERROR" in l or "runtime error" in l]
        return f"{name}: {(report or ['sanitizer report'])[0]}"
    if "panic" in path.parts:
        return None if run.returncode == 101 else f"{name}: exit status {run.returncode}"
    expected = path.with_suffix(".out")
    if expected.exists() and run.stdout != expected.read_text():
        return f"{name}: the output differs from {expected.name}"
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", default=str(ROOT / "build" / "stage0" / "ofir"))
    args = parser.parse_args()
    if not shutil.which("gcc"):
        print("asan: gcc is not installed, skipped")
        return 0
    # AddressSanitizer takes over SIGSEGV and its alternate stack, so the
    # runtime's stack overflow report cannot work under it.
    panics = [p for p in sorted((ROOT / "tests" / "panic").glob("*.of"))
              if not p.stem.startswith("stack_overflow")]
    paths = programs() + panics
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        runtime = build_runtime(out)
        with concurrent.futures.ThreadPoolExecutor() as pool:
            results = list(pool.map(lambda p: check(args.compiler, p, runtime, out), paths))
    problems = [r for r in results if r]
    for problem in problems:
        print(f"error: {problem}")
    print(f"asan: {len(results)} programs, {len(problems)} with sanitizer reports or wrong results")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
