#!/usr/bin/env python3
"""Mutation fuzzing of the compiler (roadmap 3.12): the sources of tests/,
std/ and examples/ are cut, spliced and sprinkled with tokens, and each
result is compiled up to LLVM IR and printed as a tree (`--emit=ast`).
The compiler must answer with diagnostics (exit status 0 or 1), never
crash, hang or trip a sanitizer (build/stage0/ofir is built with ASan and
UBSan). Failing inputs are kept in the output directory.

    python3 tools/fuzz_frontend.py [--compiler build/stage0/ofir] [--count 500] [--seed 1]
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOKENS = ["{", "}", "(", ")", "@test\n", '"', "fn ", "match x {", ".", "?", "&mut ", "[", "]",
          "move ", "\n", " as u8", "<T>", "0x", '"{', "/*", "self", "_ =>"]


def executable(path: str) -> str:
    """The compiler as an absolute path; on Windows, `.exe` may be implied."""
    full = Path(path).resolve()
    if os.name == "nt" and not full.suffix and full.with_suffix(".exe").exists():
        full = full.with_suffix(".exe")
    return str(full)


def mutate(rng: random.Random, text: str) -> str:
    for _ in range(rng.randint(1, 4)):
        if not text:
            break
        cut = rng.randrange(len(text))
        mode = rng.randrange(4)
        if mode == 0:
            text = text[:cut]
        elif mode == 1:
            text = text[:cut] + text[cut + rng.randrange(1, 40):]
        elif mode == 2:
            text = text[:cut] + rng.choice(TOKENS) + text[cut:]
        else:
            start = rng.randrange(len(text))
            text = text[:cut] + text[start:start + 30] + text[cut:]
    return text


def crashes(compiler: str, case: Path, emit: str, out: Path) -> bool:
    try:
        run = subprocess.run([compiler, "build", str(case), emit, "-o", str(out / "case")],
                             capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return True
    return run.returncode not in (0, 1) or "Sanitizer" in run.stderr or \
        "runtime error:" in run.stderr


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", type=executable,
                        default=str(ROOT / "build" / "stage0" / "ofir"))
    parser.add_argument("--count", type=int, default=500)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--out", default=tempfile.mkdtemp(prefix="ofir-fuzz-"))
    args = parser.parse_args()
    rng = random.Random(args.seed)
    sources = [p for d in ("tests", "std", "examples") for p in sorted((ROOT / d).rglob("*.of"))]
    out = Path(args.out)
    failures = 0
    for i in range(args.count):
        text = mutate(rng, rng.choice(sources).read_text(errors="replace"))
        case = out / f"case{i}.of"
        case.write_text(text)
        bad = any(crashes(args.compiler, case, emit, out) for emit in ("--emit=llvm", "--emit=ast"))
        if bad:
            failures += 1
            print(f"error: {case} crashed or hung the compiler")
        else:
            case.unlink()
    print(f"fuzz: {args.count} inputs, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
