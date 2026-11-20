#!/usr/bin/env python3
"""Differential testing over the whole language (roadmap 6.33j, ADR 0103).

Generates well-typed programs of structs, enums and `match`, tuples, lists,
strings, `for` ranges and loops, `if`, calls and wrapping integer
arithmetic, with no expected output: the output of stage 0 in `dev` is the
reference, and stage 0 `--release`, stage 1 `dev` and `--release` and the
AIR interpreter (each call evaluated with `comptime` by stage 1) must print
the same. The programs avoid panics (wrapping operations, divisors
`(x & 7) + 1`, indexes reduced modulo the length) so that `comptime` can
evaluate every call. A program that differs is minimized, removing
calls, functions and statements (or setting a variable to 0) while it
still compiles and still differs, then written with its output to the
output directory, ready to become a test under tests/run/fuzz/.

    python3 tools/diff_programs.py [--compiler build/stage0/ofir] [--stage1 build/stage1/ofir]
        [--count 20] [--seed 1] [--out DIR]
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRELUDE = """struct P {
    x: int
    y: int
}

enum E {
    A(int)
    B(int, int)
    C
}
"""
LITERALS = ["0", "1", "2", "3", "7", "-1", "-5", "100", "-100", "65535",
            "9223372036854775807", "-9223372036854775807"]
WRAPPING = ["wrappingAdd", "wrappingSub", "wrappingMul"]
CMP = ["==", "!=", "<", "<=", ">", ">="]


@dataclass
class Function:
    name: str
    returns: str              # "int" or "str"
    body: list[tuple[str, list[str]]]   # statements: the variable each defines, its lines
    result: str               # the returned expression

    def source(self) -> str:
        lines = [f"fn {self.name}(a: int, b: int) {self.returns} {{"]
        for _, stmt in self.body:
            lines += ["    " + line for line in stmt]
        lines += [f"    return {self.result}", "}"]
        return "\n".join(lines) + "\n"


@dataclass
class Program:
    functions: list[Function]
    calls: list[tuple[str, int, int]]

    def source(self, comptime: bool = False) -> str:
        parts = [PRELUDE] + [f.source() for f in self.functions]
        main = ["fn main() {"]
        for name, x, y in self.calls:
            call = f"{name}({x}, {y})"
            main.append(f'    println("{{{"comptime " + call if comptime else call}}}")')
        main.append("}")
        return "\n".join(parts + ["\n".join(main) + "\n"])


class Generator:
    def __init__(self, rng: random.Random):
        self.rng = rng

    def expr(self, names: list[str], calls: list[Function], depth: int) -> str:
        r = self.rng
        if depth <= 0 or r.random() < 0.3:
            return r.choice(names) if r.random() < 0.75 else r.choice(LITERALS)
        a = self.expr(names, calls, depth - 1)
        b = self.expr(names, calls, depth - 1)
        kind = r.randrange(9)
        if kind < 3:
            return f"({a}).{r.choice(WRAPPING)}({b})"
        if kind == 3:
            return f"({a} {r.choice(['/', '%'])} (({b} & 7) + 1))"
        if kind == 4:
            return f"({a} {r.choice(['&', '|', '^'])} {b})"
        if kind == 5:
            return f"({a} >> ({b} & 63))"
        if kind == 6:
            return f"(if {self.cond(names, calls, depth - 1)} {{ {a} }} else {{ {b} }})"
        if kind == 7 and calls:
            f = r.choice(calls)
            call = f"{f.name}({a}, {b})"
            return call if f.returns == "int" else f"{call}.len()"
        return f"({a}, {b}).{r.randrange(2)}"

    def cond(self, names: list[str], calls: list[Function], depth: int) -> str:
        r = self.rng
        a = self.expr(names, calls, depth)
        b = self.expr(names, calls, depth)
        c = f"{a} {r.choice(CMP)} {b}"
        kind = r.randrange(5)
        if kind == 0 and depth > 0:
            return f"({c} && {self.cond(names, calls, depth - 1)})"
        if kind == 1 and depth > 0:
            return f"({c} || {self.cond(names, calls, depth - 1)})"
        if kind == 2:
            return f"!({c})"
        return c

    def statement(self, n: int, names: list[str], mutable: list[str],
                  calls: list[Function]) -> tuple[list[str], str]:
        """A statement that defines the variable `v{n}`, and its lines."""
        r = self.rng
        e = lambda d=2, extra=(): self.expr(names + list(extra), calls, d)
        v = f"v{n}"
        kind = r.randrange(10)
        if kind == 0 and mutable:
            target = r.choice(mutable)
            return [f"let {v} = {e()}", f"{target} = {e(2, [v])}"], v
        if kind == 1:
            count = r.randrange(1, 6)
            return [f"let {v} = {e()}", f"for i{n} in 0..{count} {{",
                    f"    {v} = {e(2, [v, f'i{n}'])}", "}"], v
        if kind == 2:
            count = r.randrange(1, 5)
            return [f"let {v} = {e()}", f"let w{n} = 0", f"for w{n} < {count} {{",
                    f"    {v} = {e(2, [v, f'w{n}'])}", f"    w{n} += 1", "}"], v
        if kind == 3:
            count = r.randrange(1, 6)
            return [f"let l{n}: List<int> = []", f"for i{n} in 0..{count} {{",
                    f"    l{n}.push({e(2, [f'i{n}'])})", "}",
                    f"let {v} = l{n}[({e()} & 1023) % l{n}.len()]",
                    f"for x{n} in l{n} {{",
                    f"    {v} = {e(1, [v, f'x{n}'])}", "}"], v
        if kind == 4:
            return [f"const p{n} = P{{x: {e()}, y: {e()}}}",
                    f"const {v} = {e(2, [f'p{n}.x', f'p{n}.y'])}"], v
        if kind == 5:
            return [f"const e{n} = if {self.cond(names, calls, 1)} {{ E.A({e()}) }} "
                    f"else if {self.cond(names, calls, 1)} {{ E.C }} else {{ E.B({e()}, {e()}) }}",
                    f"const {v} = match e{n} {{",
                    f"    E.A(u{n}) => {e(2, [f'u{n}'])}",
                    f"    E.B(u{n}, t{n}) => {e(2, [f'u{n}', f't{n}'])}",
                    f"    E.C => {e()}", "}"], v
        if kind == 6:
            return [f"const t{n} = ({e()}, {self.cond(names, calls, 1)})",
                    f"const {v} = if t{n}.1 {{ t{n}.0 }} else {{ {e(2, [f't{n}.0'])} }}"], v
        if kind == 7:
            return [f'const s{n} = "{{{e()}}}-{{{self.cond(names, calls, 1)}}}" + "z"',
                    f"const {v} = s{n}.len().wrappingAdd({e(1)})"], v
        if kind == 8 and mutable:
            target = r.choice(mutable)
            return [f"let {v} = {e()}", f"if {self.cond(names, calls, 1)} {{",
                    f"    {target} = {e(2, [v])}", "} else {",
                    f"    {v} = {e(2, [target])}", "}"], v
        return [f"let {v} = {e(3)}"], v

    def function(self, index: int, calls: list[Function]) -> Function:
        names = ["a", "b"]
        mutable: list[str] = []
        body = []
        for n in range(self.rng.randrange(2, 7)):
            lines, v = self.statement(n, names, mutable, calls)
            body.append((v, lines))
            names.append(v)
            if lines[0].startswith(f"let {v} "):
                mutable.append(v)
        if self.rng.random() < 0.3:
            picks = self.rng.sample(names, min(3, len(names)))
            result = '"' + ":".join("{" + p + "}" for p in picks) + '"'
            return Function(f"f{index}", "str", body, result)
        return Function(f"f{index}", "int", body, self.expr(names, calls, 3))

    def program(self, functions: int, calls: int) -> Program:
        fns: list[Function] = []
        for i in range(functions):
            fns.append(self.function(i, fns))
        args = lambda: self.rng.choice([self.rng.randrange(-20, 20), int(self.rng.choice(LITERALS))])
        return Program(fns, [(self.rng.choice(fns).name, args(), args()) for _ in range(calls)])


def executable(path: str) -> str:
    full = Path(path).resolve()
    if os.name == "nt" and not full.suffix and full.with_suffix(".exe").exists():
        full = full.with_suffix(".exe")
    return str(full)


@dataclass
class Mode:
    name: str
    compiler: str
    flags: list[str] = field(default_factory=list)
    comptime: bool = False


def outcome(mode: Mode, prog: Program, case: Path) -> tuple[int, str]:
    case.write_text(prog.source(mode.comptime))
    try:
        run = subprocess.run([mode.compiler, "run", str(case), *mode.flags],
                             capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return -1, "timeout"
    return run.returncode, run.stdout if run.returncode == 0 else run.stderr.strip()[:400]


def difference(prog: Program, reference: Mode, modes: list[Mode], case: Path) -> str | None:
    """How one of the modes differs from the reference, or None; a program
    the reference does not run is not a difference."""
    want = outcome(reference, prog, case)
    if want[0] != 0:
        return None
    for mode in modes:
        got = outcome(mode, prog, case)
        if got != want:
            return f"{mode.name}: expected {want!r}, got {got!r}"
    return None


def minimize(prog: Program, reference: Mode, mode: Mode, case: Path) -> Program:
    """Remove calls, then functions, then statements or replace them with
    `let v = 0`, while the program still differs in this mode."""
    def fails(p: Program) -> bool:
        return difference(p, reference, [mode], case) is not None

    for call in prog.calls:
        trial = Program(prog.functions, [call])
        if fails(trial):
            prog = trial
            break
    changed = True
    while changed:
        changed = False
        for i in range(len(prog.functions)):
            trial = Program(prog.functions[:i] + prog.functions[i + 1:], prog.calls)
            if fails(trial):
                prog, changed = trial, True
                break
        if changed:
            continue
        for i, fn in enumerate(prog.functions):
            for j, (v, lines) in enumerate(fn.body):
                rest = fn.body[j + 1:]
                for body in (fn.body[:j] + rest, fn.body[:j] + [(v, [f"let {v} = 0"])] + rest):
                    if body[j:j + 1] == fn.body[j:j + 1]:
                        continue
                    smaller = Function(fn.name, fn.returns, body, fn.result)
                    trial = Program(prog.functions[:i] + [smaller] + prog.functions[i + 1:],
                                    prog.calls)
                    if fails(trial):
                        prog, changed = trial, True
                        break
                if changed:
                    break
            if changed:
                break
    return prog


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", type=executable,
                        default=str(ROOT / "build" / "stage0" / "ofir"))
    parser.add_argument("--stage1", type=executable, default=None)
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--functions", type=int, default=5)
    parser.add_argument("--calls", type=int, default=6)
    parser.add_argument("--out", default=tempfile.mkdtemp(prefix="ofir-diff-programs-"))
    args = parser.parse_args()
    os.environ.setdefault("OFIR_STD", str(ROOT / "std"))
    reference = Mode("stage0 dev", args.compiler)
    modes = [Mode("stage0 release", args.compiler, ["--release"])]
    if args.stage1:
        modes += [Mode("stage1 dev", args.stage1), Mode("stage1 release", args.stage1, ["--release"]),
                  Mode("stage1 comptime", args.stage1, comptime=True)]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    gen = Generator(random.Random(args.seed))
    failures = 0
    invalid = 0
    for i in range(args.count):
        prog = gen.program(args.functions, args.calls)
        case = out / f"program{i}.of"
        if outcome(reference, prog, case)[0] != 0:
            invalid += 1   # the generator's bug, not the compiler's: kept for a look
            print(f"error: program {i} does not run on {reference.name}: {case}")
            continue
        for mode in modes:
            problem = difference(prog, reference, [mode], case)
            if problem is None:
                continue
            failures += 1
            small = minimize(prog, reference, mode, out / f"min{i}.of")
            name = out / f"diff{args.seed}_{i}.of"
            name.write_text(small.source())
            name.with_suffix(".out").write_text(outcome(reference, small, out / f"min{i}.of")[1])
            print(f"error: program {i}, {problem}\n  minimized: {name}")
            break
        case.unlink(missing_ok=True)
        (out / f"min{i}.of").unlink(missing_ok=True)
    print(f"diff-programs: {args.count} programs, {len(modes) + 1} modes, "
          f"{failures} differ, {invalid} invalid")
    return 1 if failures or invalid else 0


if __name__ == "__main__":
    sys.exit(main())
