#!/usr/bin/env python3
"""Compares the stage 1 compiler (compiler/, built by stage 0) with stage 0
(roadmap phase 4): for every Ofir file of the repository, each command
that stage 1 implements so far must give the same standard output, standard
error and exit status as stage 0's matching option (after an error, the
standard error and the status only: the trees built while recovering need
not agree). `--fuzz N` also
compares N mutated sources (tools/fuzz_frontend.py's mutations). Files
marked `// requires: stage1` (stage 1 checks more, e.g. borrows) or
`// requires: stage0` (stage 0 rejects more) and the mutations of them are
skipped.

    python3 tools/compare_stages.py [--compiler build/stage0/ofir] [--fuzz 200]
"""

import argparse
import concurrent.futures
import random
import re
import subprocess
import sys
import os
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from fuzz_frontend import mutate  # noqa: E402

# what is compared -> the option of `ofir build` both stages are given
COMMANDS = {"tokens": "--emit=tokens", "ast": "--emit=ast", "names": "--emit=names",
            "types": "--emit=types", "air": "--emit=air", "llvm": "--emit=llvm"}
SOURCES = ["tests", "std", "examples", "compiler", "bench"]


# Both stages read the same standard library, so that paths in messages agree.
ENV = dict(os.environ, OFIR_STD=str(ROOT / "std"))

# Tests for one stage only: stage 1 checks more, stage 0 rejects more.
ONE_STAGE = re.compile(r"^//\s*requires:\s*stage[01]\b", re.MULTILINE)


# E0100's message: stage 0 rejects a feature that stage 1 may implement.
STAGE0_LIMIT = b"not supported by stage 0"


def executable(path: str) -> str:
    """The compiler as an absolute path; on Windows, `.exe` may be implied."""
    full = Path(path).resolve()
    if os.name == "nt" and not full.suffix and full.with_suffix(".exe").exists():
        full = full.with_suffix(".exe")
    return str(full)


def comparable(path: Path) -> bool:
    return not ONE_STAGE.search(path.read_text(errors="replace"))


def run(argv: list) -> tuple:
    result = subprocess.run(argv, capture_output=True, timeout=60, env=ENV)
    return result.returncode, result.stdout, result.stderr


def emit(compiler: str, path: Path, option: str, command: str = "build") -> tuple:
    """`ofir <command> <path> <option>`; `--emit=llvm` writes `<output>.ll`,
    read as the output."""
    if option != "--emit=llvm":
        return run([compiler, command, str(path), option, "-o", "/dev/null"])
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "out"
        status, _, stderr = run([compiler, command, str(path), option, "-o", str(exe)])
        ll = exe.with_name("out.ll")
        return status, ll.read_bytes() if ll.exists() else b"", stderr


def compare(stage0: str, stage1: str, path: Path) -> tuple:
    problems = []
    lowered = False
    for command, option in COMMANDS.items():
        expected = emit(stage0, path, option)
        actual = emit(stage1, path, option)
        lowered = lowered or (command == "air" and actual[0] == 0)
        if expected[0] != 0 and actual[0] == expected[0] and actual[2] == expected[2]:
            continue   # after errors, the trees of the recovery need not agree
        if STAGE0_LIMIT in expected[2] and STAGE0_LIMIT not in actual[2]:
            break      # a feature stage 1 has and stage 0 rejects, as interfaces
        if expected != actual:
            names = ("status", "stdout", "stderr")
            what = [n for n, a, b in zip(names, expected, actual) if a != b]
            problems.append(f"{path}: `{command}` differs from stage 0 ({', '.join(what)})")
    if path.name.endswith("_test.of"):   # `ofir test`: the tests and their entry point
        for option in ("--emit=air", "--emit=llvm"):
            if emit(stage0, path, option, "test") != emit(stage1, path, option, "test"):
                problems.append(f"{path}: `test {option}` differs from stage 0")
    return problems, lowered


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", type=executable,
                        default=str(ROOT / "build" / "stage0" / "ofir"))
    parser.add_argument("--stage1", type=executable,
                        help="use an already built stage 1 compiler")
    parser.add_argument("--fuzz", type=int, default=0)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        stage1 = args.stage1 or str(Path(tmp) / "ofir1")
        if not args.stage1:
            built = run([args.compiler, "build", str(ROOT / "compiler" / "main.of"), "-o", stage1])
            if built[0] != 0:
                sys.stderr.write(built[2].decode())
                print("stages: stage 1 does not build")
                return 1
        files = sorted(p for d in SOURCES for p in (ROOT / d).rglob("*.of") if comparable(p))
        rng = random.Random(args.seed)
        for i in range(args.fuzz):
            case = Path(tmp) / f"fuzz{i}.of"
            case.write_text(mutate(rng, rng.choice(files).read_text(errors="replace")))
            files.append(case)
        with concurrent.futures.ThreadPoolExecutor() as pool:
            results = list(pool.map(lambda p: compare(args.compiler, stage1, p), files))
    problems = [p for r, _ in results for p in r]
    lowered = sum(1 for _, ok in results if ok)
    for problem in problems:
        print(f"error: {problem}")
    print(f"stages: {len(files)} files ({args.fuzz} mutated), commands {', '.join(COMMANDS)}: "
          f"{len(problems)} differences; {lowered} programs lowered")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
