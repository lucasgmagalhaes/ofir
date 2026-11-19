#!/usr/bin/env python3
"""Differential and property testing of the compiler (OFIR-73, ADR 0086):
random programs of checked integer arithmetic, branches and loops are
generated with the output and panic that the specification gives them
(chapter 2, 4.10), computed here by an independent evaluator. Each program
is run by every compiler and profile given (stage 0 and stage 1, `dev` and
`--release`); a result that differs from the evaluator's is a failure. A
failing program is minimized, then written with its expected output to
the output directory, ready to become a test under tests/run/fuzz/ or
tests/panic/fuzz/.

    python3 tools/fuzz_programs.py [--compiler build/stage0/ofir] [--stage1 build/stage1/ofir]
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
PANIC_EXIT_STATUS = 101
TYPES = {"i8": (8, True), "i32": (32, True), "i64": (64, True),
         "u8": (8, False), "u32": (32, False), "u64": (64, False)}
ARITH = ["+", "-", "*", "/", "%"]
BITS = ["&", "|", "^"]
CMP = ["==", "!=", "<", "<=", ">", ">="]
WRAPPING = ["wrappingAdd", "wrappingSub", "wrappingMul"]
OVERFLOW = "integer overflow"
DIVISION = "division by zero"
PANIC_RATE = 0.3


class Panic(Exception):
    """A checked operation failed; the message is the runtime's."""


def bounds(ty: str) -> tuple[int, int]:
    width, signed = TYPES[ty]
    return (-(1 << (width - 1)), (1 << (width - 1)) - 1) if signed else (0, (1 << width) - 1)


def wrap(ty: str, v: int) -> int:
    width, signed = TYPES[ty]
    v &= (1 << width) - 1
    return v - (1 << width) if signed and v >> (width - 1) else v


def checked(ty: str, v: int) -> int:
    lo, hi = bounds(ty)
    if not lo <= v <= hi:
        raise Panic(OVERFLOW)
    return v


def divide(ty: str, op: str, a: int, b: int) -> int:
    if b == 0:
        raise Panic(DIVISION)
    q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
    checked(ty, q)  # MIN / -1 and MIN % -1 overflow
    return q if op == "/" else a - q * b


# Expressions are tuples: ("var", name), ("lit", v), ("bin", op, l, r),
# ("neg", e), ("shift", op, e, k), ("call", method, l, r).
def evaluate(ty: str, e: tuple, env: dict) -> int:
    kind = e[0]
    if kind == "var":
        return env[e[1]]
    if kind == "lit":
        return e[1]
    if kind == "neg":
        return checked(ty, -evaluate(ty, e[1], env))
    if kind == "shift":
        v = evaluate(ty, e[2], env)
        return wrap(ty, v << e[3]) if e[1] == "<<" else v >> e[3]
    a = evaluate(ty, e[2], env)
    b = evaluate(ty, e[3], env)
    if kind == "call":
        if e[1] in ("min", "max"):
            return min(a, b) if e[1] == "min" else max(a, b)
        if e[1] == "wrappingAdd":
            return wrap(ty, a + b)
        return wrap(ty, a - b if e[1] == "wrappingSub" else a * b)
    op = e[1]
    if op in ("/", "%"):
        return divide(ty, op, a, b)
    if op in BITS:
        return wrap(ty, a & b if op == "&" else a | b if op == "|" else a ^ b)
    return checked(ty, a + b if op == "+" else a - b if op == "-" else a * b)


def holds(ty: str, c: tuple, env: dict) -> bool:
    if c[0] in ("&&", "||"):
        left = holds(ty, c[1], env)
        if (c[0] == "&&") != left:
            return left
        return holds(ty, c[2], env)
    a, b = evaluate(ty, c[1], env), evaluate(ty, c[2], env)
    return {"==": a == b, "!=": a != b, "<": a < b, "<=": a <= b, ">": a > b, ">=": a >= b}[c[0]]


def show(ty: str, e: tuple, context: bool = True) -> str:
    """Ofir source for e. Without a `context` (an expected type), as in a
    method receiver or a comparison, a literal would be `int` and the left
    operand of a binary operator types the right, so literals there are
    written with `as`."""
    kind = e[0]
    if kind == "var":
        return e[1]
    if kind == "lit":
        if not context:
            return f"(({e[1]}) as {ty})" if e[1] < 1 << 63 else f"(0 as {ty} + {e[1]})"
        return f"({e[1]})" if e[1] < 0 else str(e[1])
    if kind == "neg":
        return f"-({show(ty, e[1], context)})"
    if kind == "shift":
        return f"({show(ty, e[2], context)} {e[1]} {e[3]})"
    if kind == "call":
        return f"({show(ty, e[2], False)}).{e[1]}({show(ty, e[3])})"
    return f"({show(ty, e[2], context)} {e[1]} {show(ty, e[3])})"


def show_cond(ty: str, c: tuple) -> str:
    if c[0] in ("&&", "||"):
        return f"({show_cond(ty, c[1])} {c[0]} {show_cond(ty, c[2])})"
    return f"{show(ty, c[1], False)} {c[0]} {show(ty, c[2])}"


@dataclass
class Function:
    """`fn name(a: T, b: T, c: T) T`: x = init; if cond { x = then } else
    { x = other }; `rounds` times x = step; return result."""
    name: str
    ty: str
    init: tuple
    cond: tuple
    then: tuple
    other: tuple
    rounds: int
    step: tuple
    result: tuple

    def run(self, args: list[int]) -> int:
        env = dict(zip("abc", args))
        env["x"] = evaluate(self.ty, self.init, env)
        branch = self.then if holds(self.ty, self.cond, env) else self.other
        env["x"] = evaluate(self.ty, branch, env)
        for _ in range(self.rounds):
            env["x"] = evaluate(self.ty, self.step, env)
        return evaluate(self.ty, self.result, env)

    def source(self) -> str:
        t = self.ty
        lines = [f"fn {self.name}(a: {t}, b: {t}, c: {t}) {t} {{",
                 f"    let x: {t} = {show(t, self.init)}",
                 f"    if {show_cond(t, self.cond)} {{",
                 f"        x = {show(t, self.then)}",
                 "    } else {",
                 f"        x = {show(t, self.other)}",
                 "    }"]
        if self.rounds:
            lines += [f"    for _ in 0..{self.rounds} {{",
                      f"        x = {show(t, self.step)}",
                      "    }"]
        lines += [f"    return {show(t, self.result)}", "}"]
        return "\n".join(lines)


@dataclass
class Program:
    functions: list[Function]
    calls: list[tuple[int, list[int]]] = field(default_factory=list)

    def expected(self) -> tuple[str, str | None]:
        """The output, and the panic message when a call panics (the last)."""
        out = []
        for i, (f, args) in enumerate(self.calls):
            try:
                out.append(f"r{i} = {self.functions[f].run(args)}\n")
            except Panic as panic:
                return "".join(out), str(panic)
        return "".join(out), None

    def source(self) -> str:
        used = sorted({f for f, _ in self.calls})
        parts = [self.functions[f].source() for f in used]
        main = ["fn main() {"]
        for i, (f, args) in enumerate(self.calls):
            shown = ", ".join(f"({v})" if v < 0 else str(v) for v in args)
            main.append(f"    const r{i} = {self.functions[f].name}({shown})")
            main.append(f'    println("r{i} = {{r{i}}}")')
        main.append("}")
        return "// Generated by tools/fuzz_programs.py.\n\n" + \
            "\n\n".join(parts + ["\n".join(main)]) + "\n"


class Generator:
    def __init__(self, rng: random.Random):
        self.rng = rng

    def literal(self, ty: str) -> tuple:
        lo, hi = bounds(ty)
        r = self.rng
        pick = r.randrange(4)
        if pick == 0:
            v = r.choice([0, 1, 2, -1, -2, hi, hi - 1, lo + 1])
        elif pick == 3:
            v = r.randint(lo + 1, hi)
        else:
            v = r.randint(-16, 16)
        return ("lit", min(max(v, lo + 1), hi))

    def expr(self, ty: str, names: str, depth: int) -> tuple:
        r = self.rng
        if depth == 0 or r.random() < 0.25:
            return ("var", r.choice(names)) if r.random() < 0.7 else self.literal(ty)
        width, signed = TYPES[ty]
        pick = r.randrange(10)
        sub = lambda: self.expr(ty, names, depth - 1)  # noqa: E731
        if pick < 5:
            return ("bin", r.choice(ARITH), sub(), sub())
        if pick == 5:
            return ("bin", r.choice(BITS), sub(), sub())
        if pick == 6:
            return ("shift", r.choice(["<<", ">>"]), sub(), r.randrange(width))
        if pick == 7 and signed:
            return ("neg", sub())
        return ("call", r.choice(WRAPPING + ["min", "max"]), sub(), sub())

    def cond(self, ty: str, names: str, depth: int) -> tuple:
        r = self.rng
        if depth > 0 and r.random() < 0.3:
            return (r.choice(["&&", "||"]), self.cond(ty, names, depth - 1),
                    self.cond(ty, names, depth - 1))
        return (r.choice(CMP), self.expr(ty, names, 2), self.expr(ty, names, 2))

    def function(self, name: str) -> Function:
        r = self.rng
        ty = r.choice(list(TYPES))
        return Function(name, ty, self.expr(ty, "abc", 3), self.cond(ty, "abcx", 1),
                        self.expr(ty, "abcx", 3), self.expr(ty, "abcx", 3), r.randrange(4),
                        self.expr(ty, "abcx", 2), self.expr(ty, "abcx", 3))

    def program(self, functions: int, calls: int) -> Program:
        fns = [self.function(f"f{i}") for i in range(functions)]
        prog = Program(fns)
        # Calls that panic are retried, unless this program is one of the
        # PANIC_RATE that end with a panic.
        ends_with_panic = self.rng.random() < PANIC_RATE
        for _ in range(calls * 4):
            if len(prog.calls) == calls:
                break
            f = self.rng.randrange(functions)
            prog.calls.append((f, [self.literal(fns[f].ty)[1] for _ in range(3)]))
            if prog.expected()[1] is not None:
                if ends_with_panic:
                    break
                prog.calls.pop()
        return prog


def executable(path: str) -> str:
    full = Path(path).resolve()
    if os.name == "nt" and not full.suffix and full.with_suffix(".exe").exists():
        full = full.with_suffix(".exe")
    return str(full)


@dataclass
class Mode:
    name: str
    compiler: str
    flags: list[str]


def outcome(mode: Mode, case: Path) -> tuple[str, str | None]:
    """The program's output and panic message, or a description of how
    the compiler failed (in the message)."""
    try:
        run = subprocess.run([mode.compiler, "run", str(case), *mode.flags],
                             capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return "", "timeout"
    if run.returncode == 0:
        return run.stdout, None
    if run.returncode == PANIC_EXIT_STATUS:
        for line in run.stderr.splitlines():
            if line.startswith("panic: "):
                return run.stdout, line[len("panic: "):]
    return run.stdout, f"exit status {run.returncode}: {run.stderr.strip()[:400]}"


def mismatch(prog: Program, mode: Mode, case: Path) -> str | None:
    case.write_text(prog.source())
    want = prog.expected()
    got = outcome(mode, case)
    if got == want:
        return None
    return f"{mode.name}: expected {want!r}, got {got!r}"


def simpler(e: tuple) -> list[tuple]:
    """Smaller expressions to try in place of e: its operands, then e with
    one operand simplified."""
    kind = e[0]
    if kind in ("var", "lit"):
        return [("lit", 0), ("lit", 1)] if e not in (("lit", 0), ("lit", 1)) else []
    if kind == "neg":
        return [e[1]] + [("neg", s) for s in simpler(e[1])]
    if kind == "shift":
        return [e[2]] + [("shift", e[1], s, e[3]) for s in simpler(e[2])]
    return [e[2], e[3]] + [(kind, e[1], s, e[3]) for s in simpler(e[2])] + \
        [(kind, e[1], e[2], s) for s in simpler(e[3])]


def minimize(prog: Program, mode: Mode, case: Path) -> Program:
    """Keep one call and one function, then simplify its parts while the
    program still fails in this mode."""
    def fails(p: Program) -> bool:
        return mismatch(p, mode, case) is not None

    for i in range(len(prog.calls)):
        trial = Program(prog.functions, [prog.calls[i]])
        if fails(trial):
            prog = trial
            break
    f, args = prog.calls[0]
    fn = prog.functions[f]
    prog = Program([fn], [(0, args)])
    changed = True
    while changed:
        changed = False
        fn = prog.functions[0]
        candidates = [Function(**{**fn.__dict__, "rounds": 0})] if fn.rounds else []
        for part in ("init", "then", "other", "step", "result"):
            candidates += [Function(**{**fn.__dict__, part: s}) for s in simpler(getattr(fn, part))]
        for cand in candidates:
            trial = Program([cand], prog.calls)
            if fails(trial):
                prog = trial
                changed = True
                break
    return prog


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", type=executable,
                        default=str(ROOT / "build" / "stage0" / "ofir"))
    parser.add_argument("--stage1", type=executable, default=None)
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--functions", type=int, default=6)
    parser.add_argument("--calls", type=int, default=12)
    parser.add_argument("--out", default=tempfile.mkdtemp(prefix="ofir-fuzz-programs-"))
    args = parser.parse_args()
    modes = [Mode("stage0 dev", args.compiler, []),
             Mode("stage0 release", args.compiler, ["--release"])]
    if args.stage1:
        modes += [Mode("stage1 dev", args.stage1, []),
                  Mode("stage1 release", args.stage1, ["--release"])]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    gen = Generator(rng)
    failures = 0
    panics = 0
    for i in range(args.count):
        prog = gen.program(args.functions, args.calls)
        panics += prog.expected()[1] is not None
        case = out / f"program{i}.of"
        for mode in modes:
            problem = mismatch(prog, mode, case)
            if problem is None:
                continue
            failures += 1
            small = minimize(prog, mode, out / f"min{i}.of")
            text, message = small.expected()
            name = out / f"fuzz{args.seed}_{i}.of"
            header = f"// panic: {message}\n" if message else ""
            name.write_text(header + small.source())
            if not message:
                name.with_suffix(".out").write_text(text)
            print(f"error: program {i}, {problem}\n  minimized: {name}")
            break
        case.unlink(missing_ok=True)
        (out / f"min{i}.of").unlink(missing_ok=True)
    print(f"fuzz-programs: {args.count} programs ({panics} panic), {len(modes)} modes, "
          f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
