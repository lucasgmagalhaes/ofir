#!/usr/bin/env python3
"""Mutation fuzzing of the compiler (roadmap 3.12): the sources of tests/,
std/ and examples/ are cut, spliced and sprinkled with tokens, and each
result is compiled up to LLVM IR and printed as a tree (`--emit=ast`).
The compiler must answer with diagnostics (exit status 0 or 1), never
crash, hang or trip a sanitizer (build/stage0/ofir is built with ASan and
UBSan). Failing inputs are kept in the output directory; `--minimize`
shrinks each one (lines, then characters) to a regression seed for
tests/fuzz/frontend/ (ADR 0086). The seeds there are compiled first: none
may crash.

    python3 tools/fuzz_frontend.py [--compiler build/stage0/ofir] [--count 500] [--seed 1]
                                   [--minimize] [--out DIR]
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
          "move ", "\n", " as u8", "<T>", "0x", '"{', "/*", "self", "_ =>",
          # aimed at the checker: types, borrows, generics, patterns, impls
          ": i64", ": &str", "&", "*", " = ", "return ", "let y = x;", "impl ", "trait ",
          "where T: ", "Option<", "Some(", "None", ".clone()", "?.", " else ", "for x in ",
          "0..", "if ", "{}", "const ", "struct S { x: i32 }\n", "enum E { A, B }\n"]
SEEDS = ROOT / "tests" / "fuzz" / "frontend"


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
                             capture_output=True, text=True, errors="replace",
                             timeout=60)
    except subprocess.TimeoutExpired:
        return True
    return run.returncode not in (0, 1) or "Sanitizer" in run.stderr or \
        "runtime error:" in run.stderr


def failing(compiler: str, case: Path, out: Path) -> bool:
    return any(crashes(compiler, case, emit, out) for emit in ("--emit=llvm", "--emit=ast"))


def ddmin(parts: list, fails) -> list:
    """Delta debugging: the smallest list of parts found that still fails."""
    n = 2
    while len(parts) >= 2:
        size = max(1, len(parts) // n)
        chunks = [parts[i:i + size] for i in range(0, len(parts), size)]
        for i in range(len(chunks)):
            rest = [x for c in chunks[:i] + chunks[i + 1:] for x in c]
            if fails(rest):
                parts, n = rest, max(n - 1, 2)
                break
        else:
            if n >= len(parts):
                break
            n = min(len(parts), n * 2)
    return parts


def minimize(compiler: str, case: Path, out: Path) -> str:
    """Shrinks a failing input in place, by lines and then by characters."""
    probe = out / "probe.of"

    def fails(text: str) -> bool:
        probe.write_text(text)
        return failing(compiler, probe, out)

    text = case.read_text(errors="replace")
    text = "".join(ddmin(text.splitlines(keepends=True), lambda p: fails("".join(p))))
    text = "".join(ddmin(list(text), lambda p: fails("".join(p))))
    probe.unlink(missing_ok=True)
    case.write_text(text)
    return text


def replay(compiler: str, out: Path) -> int:
    """The regression seeds: inputs that once crashed a stage must not again."""
    failures = 0
    for seed in sorted(SEEDS.glob("*.of")):
        if failing(compiler, seed, out):
            failures += 1
            print(f"error: seed {seed.relative_to(ROOT)} crashed or hung the compiler")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", type=executable,
                        default=str(ROOT / "build" / "stage0" / "ofir"))
    parser.add_argument("--count", type=int, default=500)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--minimize", action="store_true",
                        help="shrink each failing input to a regression seed")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    sources = [p for d in ("tests", "std", "examples") for p in sorted((ROOT / d).rglob("*.of"))]
    out = Path(args.out or tempfile.mkdtemp(prefix="ofir-fuzz-"))
    out.mkdir(parents=True, exist_ok=True)
    seeds = len(list(SEEDS.glob("*.of")))
    failures = replay(args.compiler, out)
    for i in range(args.count):
        text = mutate(rng, rng.choice(sources).read_text(errors="replace"))
        case = out / f"case{args.seed}_{i}.of"
        case.write_text(text)
        if failing(args.compiler, case, out):
            failures += 1
            if args.minimize:
                minimize(args.compiler, case, out)
            print(f"error: {case} crashed or hung the compiler")
        else:
            case.unlink()
    print(f"fuzz: {seeds} seeds, {args.count} inputs, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
