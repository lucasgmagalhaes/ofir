#!/usr/bin/env python3
"""Measures the line and branch coverage of both compilers (ADR 0103).

Stage 0 is built by gcc with `--coverage`; stage 1 is built by stage 1
with `--release --coverage` (ADR 0073), which writes gcov counters that
`llvm-cov gcov` reads. Each then runs the suites `make test` runs with it:

- stage 0: the bootstrap unit tests, the test runner, the std tests,
  `compare_stages.py` and the frontend fuzzer;
- stage 1: the test runner, then every tool of the `test-stage1` recipe of
  the Makefile (read from it, so the two cannot drift), `compare_stages.py`
  and the frontend fuzzer;
- programs: the run tests and std tests, built by stage 1 with
  `--coverage` and linked with a runtime built by clang with
  `--coverage`, which measure `runtime/src` and `std/`.

gcovr then writes a Cobertura report per stage, which CI uploads to
Codecov with the flags `stage0`, `stage1`, `runtime` and `std` (codecov.yml), and a JSON
summary. Needs gcc, gcovr, `llvm-cov` and clang's profile runtime.

`--check` reads those reports and fails when a directory's line coverage
is below its floor in `tools/coverage_floors.json`, or when an exclusion
marker gives no reason; `--update-floors` raises the floors to the
current coverage. Neither measures: run them after a measurement.

    python3 tools/coverage.py [--only stage0|stage1|programs] [--out build/coverage]
    python3 tools/coverage.py --check | --update-floors [--out build/coverage]
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
EXE = ".exe" if os.name == "nt" else ""
JOBS = str(min(8, os.cpu_count() or 1))
GCOV = os.environ.get("OFIR_LLVM_COV", "llvm-cov") + " gcov"
CFLAGS = "-std=c11 -D_POSIX_C_SOURCE=200809L -Isrc -Itests -O0 -g --coverage"


def run(cmd, env=None, cwd=ROOT):
    """Runs a command; a failure stops the measurement."""
    shown = " ".join(str(part) for part in cmd)
    print(f"coverage: {shown}", flush=True)
    result = subprocess.run([str(part) for part in cmd], cwd=cwd, env=env)
    if result.returncode != 0:
        sys.exit(f"coverage: failed ({result.returncode}): {shown}")


def stage_env():
    env = dict(os.environ)
    env["OFIR_STD"] = str(ROOT / "std")
    env["OFIR_RUNTIME"] = str(BUILD / "libofir_rt.a")
    return env


def tool_commands(compiler):
    """The tool scripts of the Makefile's `test-stage1` recipe, but the
    test runner and the std tests, with the coverage compiler in place of
    stage 1."""
    text = (ROOT / "Makefile").read_text().replace("\\\n", " ")
    lines = text.split("\ntest-stage1:", 1)[1].split("\n\n", 1)[0].splitlines()[1:]
    commands = []
    for line in lines:
        match = re.search(r"\$\(PYTHON\) (tools/\S+\.py)(.*)", line)
        if not match or match.group(1) == "tools/run_std_tests.py":
            continue
        args = match.group(2).replace("$(BUILD)/stage1/ofir$(EXE)", str(compiler))
        args = args.replace("$(BUILD)", str(BUILD)).split()
        commands.append([sys.executable, match.group(1), *args])
    return commands


def frontends(compiler, stage, env, other=None):
    """The suites both stages run: tests, std tests, stage parity, fuzz."""
    run([sys.executable, "tests/runner/run_tests.py", "--compiler", compiler,
         "--stage", stage, "-j", JOBS], env)
    run([sys.executable, "tools/run_std_tests.py", "--compiler", compiler,
         "--stage", stage], env)
    if other is not None:
        stage0, stage1 = (compiler, other) if stage == "0" else (other, compiler)
        run([sys.executable, "tools/compare_stages.py", "--compiler", stage0,
             "--stage1", stage1, "--fuzz", "200"], env)
    run([sys.executable, "tools/fuzz_frontend.py", "--compiler", compiler,
         "--count", "100", "--seed", "1"], env)


def stage0(work, env):
    build = work / "stage0"
    shutil.rmtree(build, ignore_errors=True)
    make = ["make", "-s", "-C", "bootstrap", f"BUILD={build}", "CC=gcc", "SANITIZE=",
            f"CFLAGS={CFLAGS}", "LDFLAGS=--coverage"]
    run(make)
    run([*make, "test"])
    frontends(build / f"ofir{EXE}", "0", env, BUILD / "stage1" / f"ofir{EXE}")
    # gcov finds the sources from bootstrap/, where gcc compiled them; the
    # paths of the report are made relative to the repository after.
    return [("stage0", ["gcovr", "-r", "bootstrap", "--object-directory", build / "obj",
                        "--exclude", "bootstrap/tests/", build / "obj"], ROOT, "bootstrap/")]


def stage1(work, env):
    build = work / "stage1"
    shutil.rmtree(build, ignore_errors=True)
    build.mkdir(parents=True)
    compiler = build / f"ofir{EXE}"
    # In its own directory: the counters (ofir.gcda) are written next to
    # the object, and gcovr reads them from there.
    run([BUILD / "stage1" / f"ofir{EXE}", "build", ROOT / "compiler" / "main.of",
         "--release", "--coverage", "-o", f"ofir{EXE}"], env, cwd=build)
    frontends(compiler, "1", env, BUILD / "stage0" / f"ofir{EXE}")
    for command in tool_commands(compiler):
        run(command, env)
    return [("stage1", ["gcovr", "-r", ".", "--gcov-executable", GCOV, "--object-directory",
                        build, "--filter", "compiler/", build], ROOT, "")]


# The commands that link a program get `--coverage`; `--emit` output and
# cross builds stay as they are, since the tests compare them.
WRAPPER = """#!/bin/sh
cmd="$1"
case "$cmd" in
build|run|test|bench)
    for arg in "$@"; do
        case "$arg" in --emit=*|--target=*) exec "{ofir}" "$@" ;; esac
    done
    shift
    exec "{ofir}" "$cmd" --coverage "$@" ;;
esac
exec "{ofir}" "$@"
"""


def programs(work, env):
    """The runtime and std as the test programs run them: the runtime built
    by clang with `--coverage`, and each test program built by stage 1
    with `--coverage` through a wrapper. Their counters go to TMPDIR,
    where the compiler and the test runner put the programs."""
    build = work / "programs"
    shutil.rmtree(build, ignore_errors=True)
    (build / "tmp").mkdir(parents=True)
    clang = os.environ.get("OFIR_CLANG", "clang")
    cflags = ("-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -O2 "
              "-Iinclude -fPIC --coverage")
    run(["make", "-s", "-C", "runtime", f"BUILD={build}", f"CC={clang}", f"CFLAGS={cflags}"])
    wrapper = build / "ofir"
    wrapper.write_text(WRAPPER.format(ofir=BUILD / "stage1" / f"ofir{EXE}"))
    wrapper.chmod(0o755)
    env = dict(env, TMPDIR=str(build / "tmp"), OFIR_RUNTIME=str(build / "libofir_rt.a"),
               OFIR_TEST_LDFLAGS="--coverage")
    run([sys.executable, "tests/runner/run_tests.py", "--compiler", wrapper, "--stage", "1",
         "-j", JOBS], env)
    run([sys.executable, "tools/run_std_tests.py", "--compiler", wrapper, "--stage", "1"], env)
    # clang names the runtime's sources relative to runtime/, where they
    # were compiled; std is named by absolute paths. Methods of different
    # types share a name, so their functions are kept apart.
    runtime = ["gcovr", "-r", ".", "--gcov-executable", GCOV, build / "runtime"]
    std = ["gcovr", "-r", ".", "--gcov-executable", GCOV, "--filter", "std/",
           "--merge-mode-functions=separate", build / "tmp"]
    return [("runtime", runtime, ROOT / "runtime", "runtime/"), ("std", std, ROOT, "")]


def report(name, gcovr, cwd, prefix, out):
    xml = out / f"{name}.xml"
    summary = out / f"{name}.json"
    run([*gcovr, "-j", JOBS, "--cobertura", xml, "--json-summary", summary], cwd=cwd)
    if prefix:
        text = xml.read_text().replace('filename="', f'filename="{prefix}')
        xml.write_text(text.replace(f"<source>{ROOT / prefix.rstrip('/')}</source>",
                                    f"<source>{ROOT}</source>"))
    data = json.loads(summary.read_text())
    return (f"{name}: lines {data['line_percent']}% ({data['line_covered']}/"
            f"{data['line_total']}), functions {data['function_percent']}%, "
            f"branches {data['branch_percent']}%")


FLOORS = ROOT / "tools" / "coverage_floors.json"
REPORTS = ("stage0", "stage1", "runtime", "std")
# A file is counted in the directory one level under its root; a file in
# the root itself is counted in the root.
ROOTS = ("bootstrap/src/", "compiler/", "runtime/src/", "std/")
MARKER = re.compile(r"GCOVR_EXCL_(LINE|START|BR_LINE|BR_START)\b(.*)")
SOURCES = ("bootstrap/src", "compiler", "runtime/src", "std")
# The machines that measure differ in compiler versions, which count a few
# lines differently; a floor is missed only by more than this.
TOLERANCE = 0.5


def directory(filename):
    for root in ROOTS:
        if filename.startswith(root):
            rest = filename[len(root):]
            return root + rest.split("/", 1)[0] if "/" in rest else root.rstrip("/")
    return None


def measured(out):
    """Line coverage per directory: {report: {directory: (covered, total)}}."""
    result = {}
    for name in REPORTS:
        xml = out / f"{name}.xml"
        if not xml.exists():
            sys.exit(f"coverage: {xml} is missing; run `make coverage` first")
        dirs = {}
        for cls in ET.parse(xml).iter("class"):
            group = directory(cls.get("filename"))
            lines = cls.find("lines")
            if group is None or lines is None:
                continue
            covered, total = dirs.get(group, (0, 0))
            for line in lines.iter("line"):
                total += 1
                covered += int(line.get("hits", "0")) > 0
            dirs[group] = (covered, total)
        result[name] = dirs
    return result


def percent(covered, total):
    # Rounded down, so that a floor written from it is never above it.
    return int(1000 * covered / total) / 10 if total else 100.0


def exclusions():
    """Each exclusion marker says why: `GCOVR_EXCL_LINE: <reason>`."""
    errors = []
    for top in SOURCES:
        for path in sorted((ROOT / top).rglob("*")):
            if path.suffix not in (".c", ".h", ".of") or not path.is_file():
                continue
            for number, text in enumerate(path.read_text(errors="replace").splitlines(), 1):
                match = MARKER.search(text)
                if match and not re.match(r":\s*\S", match.group(2)):
                    rel = path.relative_to(ROOT)
                    errors.append(f"{rel}:{number}: GCOVR_EXCL_{match.group(1)} gives no reason")
    return errors


def check(out):
    floors = json.loads(FLOORS.read_text())
    errors = exclusions()
    raised = []
    for name, dirs in measured(out).items():
        for group, (covered, total) in sorted(dirs.items()):
            now = percent(covered, total)
            floor = floors.get(name, {}).get(group)
            if floor is None:
                errors.append(f"{name} {group}: no floor (run --update-floors)")
            elif now < floor - TOLERANCE:
                errors.append(f"{name} {group}: lines {now}% is below the floor {floor}%")
            elif now > floor + TOLERANCE:
                raised.append(f"{name} {group}: {floor}% -> {now}%")
    if raised:
        print("coverage: floors that can be raised (--update-floors):")
        print("\n".join(f"  {line}" for line in raised))
    for error in errors:
        print(f"coverage: {error}")
    print(f"coverage: {len(errors)} errors")
    return 1 if errors else 0


def update_floors(out):
    """The floors become the current coverage, but never go down."""
    floors = json.loads(FLOORS.read_text()) if FLOORS.exists() else {}
    for name, dirs in measured(out).items():
        old = floors.get(name, {})
        floors[name] = {group: max(old.get(group, 0.0), percent(*counts))
                        for group, counts in sorted(dirs.items())}
    FLOORS.write_text(json.dumps(floors, indent=2) + "\n")
    print(f"coverage: wrote {FLOORS.relative_to(ROOT)}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", choices=["stage0", "stage1", "programs"])
    parser.add_argument("--out", type=Path, default=BUILD / "coverage")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="check the floors and exclusions")
    mode.add_argument("--update-floors", action="store_true", help="raise the floors")
    opts = parser.parse_args()
    if opts.check:
        sys.exit(check(opts.out.resolve()))
    if opts.update_floors:
        sys.exit(update_floors(opts.out.resolve()))
    if shutil.which("gcovr") is None:
        sys.exit("coverage: gcovr not found (pip install gcovr)")
    run(["make", "-s", "stage1", "runtime"])
    out = opts.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    work = BUILD / "cov"
    env = stage_env()
    lines = []
    for name, measure in (("stage0", stage0), ("stage1", stage1), ("programs", programs)):
        if opts.only in (None, name):
            for measured in measure(work, env):
                lines.append(report(*measured, out))
    print("\n".join(lines))


if __name__ == "__main__":
    main()
