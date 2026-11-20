#!/usr/bin/env python3
"""Checks the security of the compiler and the build (ADR 0092, OFIR-78).

- Budgets: each compiler is given adversarial source (random bytes, NUL
  bytes, invalid UTF-8, a 1 MiB line, deep nesting, a long operator
  chain, a 10 000-digit literal, many syntax errors) and must end with a
  diagnostic, not a signal, within the time and memory limits, with at
  most one error per byte of source;
- commands: a program whose path holds spaces and shell metacharacters
  builds and runs, and nothing runs the text of the path; no compiler or
  runtime source calls `system` or `popen`, and no tool script uses
  `shell=True`;
- paths (stage 1): a fetched package whose path dependency, native file
  or root file leaves its checkout, even through a symbolic link, is
  E0924, and one that stays inside builds.

Credentials in URLs (E0925) are covered by `tests/compile_fail` and the
cache's checksums by `tools/check_object_cache.py`.

    python3 tools/check_build_security.py --stage0 build/stage0/ofir \\
        --compiler build/stage1/ofir
"""

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TIME_LIMIT = 10        # seconds per compile
MEMORY_LIMIT = 1 << 30  # bytes of peak resident memory, where it can be measured
VALID = {"long_string"}
HELLO = 'fn main() {\n    println("hello")\n}\n'
LIB = 'pub fn hello() str {\n    return "hello"\n}\n'
EXE = ".exe" if os.name == "nt" else ""

failures = []


def check(ok: bool, what: str) -> None:
    print(f"{'ok' if ok else 'FAIL'}: {what}")
    if not ok:
        failures.append(what)


def adversarial() -> dict[str, bytes]:
    """Hostile sources, by name."""
    rng = random.Random(78)
    line = "x" * (1 << 20)
    return {
        "random": bytes(rng.randrange(256) for _ in range(1 << 16)),
        "nul": b"fn main() {\n    \0\0\0 println(\"a\0b\")\n}\n" * 1000,
        "utf8": b"fn main() {\n    println(\"\xff\xfe\xc3\")  // \xed\xa0\x80\n}\n" * 1000,
        "long_line": f"fn main() {{\n    const a = {line}\n}}\n".encode(),
        "long_string": f'fn main() {{\n    println("{line}")\n}}\n'.encode(),
        "nesting": ("fn main() {\n    const a = " + "(" * 100000 + "1" + ")" * 100000
                    + "\n}\n").encode(),
        "blocks": ("fn main() " + "{\n" * 50000 + "}\n" * 50000).encode(),
        "chain": ("fn main() {\n    const a = 1" + " + 1" * 100000 + "\n}\n").encode(),
        "digits": ("fn main() {\n    const a = " + "9" * 10000 + "\n}\n").encode(),
        "errors": b"fn main() {\n" + b"    const = ) ] }\n" * 20000 + b"}\n",
        "unterminated": b'fn main() {\n    println("\n' * 20000,
        "generics": ("fn f(x: " + "List<" * 50000 + "int" + ">" * 50000 + ") {\n}\n").encode(),
    }


def run(cmd: list[str], cwd: Path, env=None) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, stdin=subprocess.DEVNULL,
                          timeout=TIME_LIMIT * 6)


def measured(cmd: list[str], cwd: Path) -> tuple[int, bytes, int]:
    """Runs `cmd`: its status, its output and its peak memory in bytes (0 if unknown)."""
    if not hasattr(os, "wait4"):   # Windows: only time is bounded
        result = run(cmd, cwd)
        return result.returncode, result.stdout + result.stderr, 0
    with tempfile.TemporaryFile() as out:
        child = subprocess.Popen(cmd, cwd=cwd, stdout=out, stderr=out, stdin=subprocess.DEVNULL)
        deadline = time.monotonic() + TIME_LIMIT * 6
        while True:
            pid, status, usage = os.wait4(child.pid, os.WNOHANG)
            if pid:
                break
            if time.monotonic() > deadline:
                child.kill()
                child.wait()
                raise subprocess.TimeoutExpired(cmd, TIME_LIMIT * 6)
            time.sleep(0.02)
        child.returncode = os.waitstatus_to_exitcode(status)
        out.seek(0)
        # ru_maxrss is in KiB on Linux and in bytes on macOS.
        peak = usage.ru_maxrss * (1 if sys.platform == "darwin" else 1024)
        return child.returncode, out.read(), peak


def check_budgets(name: str, compiler: str, tmp: Path) -> None:
    work = tmp / f"budgets-{name}"
    work.mkdir()
    for case, source in adversarial().items():
        path = work / f"{case}.of"
        path.write_bytes(source)
        start = time.monotonic()
        try:
            status, output, peak = measured(
                [compiler, "build", path.name, "-o", f"{case}{EXE}"], work)
        except subprocess.TimeoutExpired:
            check(False, f"{name}: `{case}` ends within {TIME_LIMIT} s")
            continue
        took = time.monotonic() - start
        errors = output.count(b"error[")
        # A large but valid file may build; a broken one ends with diagnostics.
        ended = status == 0 if case in VALID else status == 1 and errors >= 1
        ok = ended and errors <= len(source) and took <= TIME_LIMIT and peak <= MEMORY_LIMIT
        check(ok, f"{name}: `{case}` ends with {errors} error(s), status {status}, "
              f"in {took:.1f} s and {peak >> 20} MiB")


def check_command_paths(name: str, compiler: str, tmp: Path) -> None:
    odd = "a b;$(touch pwned)&'x" if os.name != "nt" else "a b;$(x)&'x"
    work = tmp / f"paths-{name}" / odd
    work.mkdir(parents=True)
    (work / f"{odd}.of").write_text(HELLO)
    exe = work / f"{odd}{EXE}"
    result = run([compiler, "build", f"{odd}.of", "-o", str(exe)], work)
    output = run([str(exe)], work).stdout if exe.exists() else b""
    pwned = list(tmp.rglob("pwned"))
    check(result.returncode == 0 and output.replace(b"\r\n", b"\n") == b"hello\n" and not pwned,
          f"{name}: a path with spaces and shell metacharacters builds, and runs nothing")


def check_sources() -> None:
    call = re.compile(r"\b(system|popen|_popen|_wsystem)\s*\(")
    calls = [f"{p.relative_to(ROOT)}:{n}"
             for d in ("bootstrap/src", "runtime/src", "tools/fault")
             for p in sorted((ROOT / d).rglob("*.[ch]"))
             for n, text in enumerate(p.read_text().splitlines(), 1)
             if call.search(text) and not text.lstrip().startswith(("//", "*"))]
    check(not calls, f"no C source calls `system` or `popen` {calls or ''}")
    shell = [f"{p.relative_to(ROOT)}:{n}"
             for d in ("tools", "tests/runner")
             for p in sorted((ROOT / d).rglob("*.py")) if p.name != Path(__file__).name
             for n, text in enumerate(p.read_text().splitlines(), 1) if "shell=True" in text]
    check(not shell, f"no tool script uses `shell=True` {shell or ''}")


def check_checkouts(compiler: str, tmp: Path) -> None:
    if shutil.which("git") is None:
        print("skipped: fetched packages (needs git)")
        return
    env = dict(os.environ, OFIR_HOME=str(tmp / "home"), GIT_AUTHOR_NAME="t",
               GIT_AUTHOR_EMAIL="t@example.com", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@example.com", GIT_CONFIG_GLOBAL=os.devnull)
    outside = tmp / "outside"
    (outside / "src").mkdir(parents=True)
    (outside / "src" / "index.of").write_text(LIB)
    (outside / "ofir.json").write_text('{"name": "outside", "build": {"kind": "lib"}}\n')
    (outside / "x.c").write_text("int ofir_outside(void) { return 1; }\n")

    def package(name: str, manifest: str, links: dict[str, Path]) -> Path:
        repo = tmp / "repos" / name
        (repo / "sub" / "src").mkdir(parents=True)
        (repo / "sub" / "src" / "index.of").write_text(LIB)
        (repo / "sub" / "ofir.json").write_text('{"name": "sub", "build": {"kind": "lib"}}\n')
        if "src" not in links:
            (repo / "src").mkdir()
            (repo / "src" / "index.of").write_text(LIB)
        for link, target in links.items():
            (repo / link).symlink_to(target)
        (repo / "ofir.json").write_text(manifest)
        for words in (["init", "-q", "-b", "main"], ["add", "."], ["commit", "-q", "-m", "x"]):
            subprocess.run(["git", "-C", str(repo), *words], env=env, check=True,
                           capture_output=True)
        return repo

    def build(name: str, manifest: str, links=None) -> subprocess.CompletedProcess:
        repo = package(name, manifest, links or {})
        app = tmp / "apps" / name
        app.mkdir(parents=True)
        (app / "main.of").write_text(f"import {name}\n\nfn main() {{\n"
                                     f"    println({name}.hello())\n}}\n")
        (app / "ofir.json").write_text(
            '{"name": "app", "build": {"kind": "exe", "root": "main.of"}, "dependencies": '
            f'{{"{name}": {{"git": "{repo.as_posix()}", "branch": "main"}}}}}}\n')
        return run([compiler, "build", "main.of", "-o", f"app{EXE}"], app, env)

    def lib(deps=None, native=None) -> str:
        build = {"kind": "lib", **({"native": native} if native else {})}
        return json.dumps({"name": "p", "build": build, "dependencies": deps or {}}) + "\n"

    inside = build("inside", lib({"sub": {"path": "sub"}}))
    check(inside.returncode == 0, "a fetched package with a path dependency inside it builds")
    cases = {
        "updir": (lib({"up": {"path": ".."}}), None),
        "absolute": (lib({"o": {"path": outside.as_posix()}}), None),
    }
    if os.name != "nt":   # drive letters; git checks out symbolic links as files on Windows
        up = "../" * 40 + outside.as_posix().lstrip("/")
        cases["native"] = (lib(native=[f"{up}/x.c"]), None)
        cases["linked_dep"] = (lib({"o": {"path": "o"}}), {"o": outside})
        cases["linked_root"] = (lib(), {"src": outside / "src"})
    for name, (manifest, links) in cases.items():
        result = build(name, manifest, links)
        check(result.returncode == 1 and b"E0924" in result.stderr,
              f"a fetched package that leaves its checkout ({name}) is E0924")
        if b"E0924" not in result.stderr:
            print(result.stderr.decode(errors="replace"), file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage0", default="build/stage0/ofir")
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compilers = {"stage 0": str(Path(args.stage0 + EXE).resolve()),
                 "stage 1": str(Path(args.compiler + EXE).resolve())}
    check_sources()
    with tempfile.TemporaryDirectory(prefix="ofir-security-") as tmp:
        tmp = Path(tmp).resolve()
        for name, compiler in compilers.items():
            check_budgets(name.replace(" ", ""), compiler, tmp)
            check_command_paths(name.replace(" ", ""), compiler, tmp)
        check_checkouts(compilers["stage 1"], tmp)
    if failures:
        print(f"build security: {len(failures)} check(s) failed", file=sys.stderr)
        return 1
    print("build security: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
