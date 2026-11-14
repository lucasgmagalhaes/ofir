#!/usr/bin/env python3
"""Ofir test runner.

Discovers test files under tests/<suite>/, runs them against a compiler and
reports the results. Test formats are described in docs/design/testing.md.

Compiler interface used by the runner:

    ofir build FILE --emit=tokens          print tokens to stdout
    ofir build FILE --emit=ast             print the AST to stdout
    ofir build FILE --emit=llvm -o OUT     write OUT.ll
    ofir build FILE --message-format=short diagnostics as
                                            `file:line:col: error[E0300]: message`
    ofir run FILE                          compile and run

Standard library only; Python 3.9 or newer.
"""

from __future__ import annotations

import argparse
import hashlib
import concurrent.futures
import os
import json
import re
import signal
import shutil
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

SUITES = ["lex", "parse", "run", "compile_fail", "panic", "borrow", "codegen", "fmt", "doc",
          "lsp"]
TIMEOUT_SECONDS = 20
PANIC_EXIT_STATUS = 101

ANNOTATION_RE = re.compile(r"//\s*(error|warning)\[([EW]\d{4})\]:\s*(.*?)\s*$")
DIAGNOSTIC_RE = re.compile(r"^(.*?):(\d+):(\d+): (error|warning)\[([EW]\d{4})\]: (.*)$")
# A note of the diagnostic annotated last, on a line of its own (stage 1).
NOTE_RE = re.compile(r"^\s*//\s*note:\s*(.*?)\s*$")
REQUIRES_RE = re.compile(r"^//\s*requires:\s*(\S+)", re.MULTILINE)
# Extra compiler flags of a run or codegen test, e.g. `// flags: --release`.
FLAGS_RE = re.compile(r"^//\s*flags:\s*(.+)$", re.MULTILINE)
# The exit status a run test expects instead of 0: a number, or `abort` for a
# process ended by SIGABRT (on Windows status 3, or 0xC0000409 from the UCRT's
# fast fail), e.g. `// status: 3`.
STATUS_RE = re.compile(r"^//\s*status:\s*(\S+)\s*$", re.MULTILINE)
# A C file next to a run test, built into `lib<name>.a` for the test's
# `@link("<name>")`, e.g. `// c: c_calls.c` (a cross-language fixture).
C_FIXTURE_RE = re.compile(r"^//\s*c:\s*(\S+)\s*$", re.MULTILINE)
# A C, C++ or Python program that uses the library a run test builds with
# `// flags: --lib=static` or `--lib=shared`, e.g. `// consumer: use_calc.c`
# (ADR 0064, ADR 0070).
CONSUMER_RE = re.compile(r"^//\s*consumer:\s*(\S+)\s*$", re.MULTILINE)
PANIC_RE = re.compile(r"^//\s*panic:\s*(.*?)\s*$", re.MULTILINE)
CHECK_RE = re.compile(r"^//\s*(CHECK|CHECK-NOT):\s*(.*?)\s*$")
TOP_LEVEL_FN_RE = re.compile(r"^(pub\s+)?(unsafe\s+)?fn\b")


@dataclass
class Result:
    path: Path
    status: str  # "pass", "fail" or "skip"
    detail: str = ""


@dataclass
class Diagnostic:
    line: int
    severity: str
    code: str
    message: str
    # "ofir.json" for a diagnostic of a project's manifest, else "".
    file: str = ""


@dataclass
class Config:
    compiler: str
    stage: int
    bless: bool
    tests_dir: Path
    verbose: bool
    # `--target`: run programs built for another target (ADR 0066).
    target: str = ""
    tmp: Path = field(default_factory=lambda: Path(tempfile.mkdtemp(prefix="ofir-tests-")))


def executable(path: str) -> str:
    """The compiler as an absolute path; on Windows, `.exe` may be implied."""
    full = Path(path).resolve()
    if os.name == "nt" and not full.suffix and full.with_suffix(".exe").exists():
        full = full.with_suffix(".exe")
    return str(full)


def run_process(cmd: list[str], cwd: Path | None = None, input_text: str | None = None,
                env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(
            cmd, cwd=cwd, capture_output=True, text=True, timeout=TIMEOUT_SECONDS,
            input=input_text, env=env
        )
    except subprocess.TimeoutExpired as exc:
        message = f"timeout after {TIMEOUT_SECONDS}s"
        return subprocess.CompletedProcess(cmd, -1, exc.stdout or "", message)


def test_env(cfg: Config, path: Path) -> dict[str, str]:
    """The environment of a compiler run: each test has its own package
    cache (`$OFIR_HOME`), so that tests running at once do not share one."""
    rel = path.relative_to(cfg.tests_dir).as_posix().replace("/", "_")
    index = Path(__file__).resolve().parents[2] / "build" / "registry-fixtures" / "index"
    return {**os.environ, "OFIR_HOME": str(cfg.tmp / "home" / rel), "OFIR_REGISTRY": str(index)}


def c_tools() -> tuple[str, str]:
    """The C compiler and archiver of the C fixtures: the clang the Ofir
    driver uses (`$OFIR_CLANG`), and the `llvm-ar` next to it or `ar`."""
    clang = os.environ.get("OFIR_CLANG") or "clang"
    found = shutil.which(clang)
    if found:
        near = Path(found).parent / ("llvm-ar" + Path(found).name[len("clang"):])
        if Path(found).name.startswith("clang") and near.exists():
            return clang, str(near)
    return clang, shutil.which("llvm-ar") or "ar"


def build_c_fixtures(cfg: Config, path: Path, source: str) -> tuple[dict[str, str], str]:
    """Builds the C files a run test names (`// c: file.c`) into
    `lib<file>.a` in a directory of the test's own, and returns its
    environment with that directory on `LIBRARY_PATH`, or an error."""
    env = test_env(cfg, path)
    files = C_FIXTURE_RE.findall(source)
    if not files:
        return env, ""
    rel = path.relative_to(cfg.tests_dir).with_suffix("").as_posix().replace("/", "_")
    lib_dir = cfg.tmp / "clib" / rel
    lib_dir.mkdir(parents=True, exist_ok=True)
    clang, ar = c_tools()
    flags = ["-O2"] if "--release" in test_flags(source) else ["-O0"]
    for name in files:
        c_file = path.parent / name
        obj = lib_dir / (Path(name).stem + ".o")
        proc = run_process([clang, *flags, "-std=c11", "-Wall", "-Werror", "-c", str(c_file),
                            "-o", str(obj)])
        if proc.returncode != 0:
            return env, f"cannot compile {name}\n" + describe(proc)
        lib = lib_dir / f"lib{Path(name).stem}.a"
        lib.unlink(missing_ok=True)
        proc = run_process([ar, "rcs", str(lib), str(obj)])
        if proc.returncode != 0:
            return env, f"cannot archive {name}\n" + describe(proc)
    old = env.get("LIBRARY_PATH")
    env["LIBRARY_PATH"] = str(lib_dir) + (os.pathsep + old if old else "")
    return env, ""


def git_version_key(name: str) -> tuple[int, ...]:
    return tuple(int(part) for part in name.split("."))


def make_git_fixtures(sources: Path, dest: Path) -> None:
    """Builds the repositories that git dependency tests name: each
    directory `tests/git/<repo>/<X.Y.Z>/` is a commit tagged `vX.Y.Z` of
    `build/git-fixtures/<repo>`, in version order. Commits are dated and
    signed the same on every run, so their hashes do not change; a
    repository is made again only when its sources change."""
    if not sources.is_dir():
        return
    for repo in sorted(p for p in sources.iterdir() if p.is_dir()):
        files = sorted(f for f in repo.rglob("*") if f.is_file())
        stamp = json.dumps([[f.relative_to(repo).as_posix(), f.read_text()] for f in files])
        target = dest / repo.name
        if (target / ".stamp").exists() and (target / ".stamp").read_text() == stamp:
            continue
        work = Path(tempfile.mkdtemp(prefix=f"git-{repo.name}-", dir=dest.parent))
        env = {**os.environ, "GIT_AUTHOR_NAME": "ofir", "GIT_AUTHOR_EMAIL": "ofir@example.com",
               "GIT_COMMITTER_NAME": "ofir", "GIT_COMMITTER_EMAIL": "ofir@example.com",
               "GIT_AUTHOR_DATE": "2026-01-01T00:00:00Z",
               "GIT_COMMITTER_DATE": "2026-01-01T00:00:00Z", "GIT_CONFIG_GLOBAL": os.devnull}

        def git(*args: str) -> None:
            subprocess.run(["git", "-C", str(work), *args], env=env, check=True,
                           capture_output=True)
        git("init", "--quiet", "--initial-branch=main")
        for version in sorted((v for v in repo.iterdir() if v.is_dir()),
                              key=lambda v: git_version_key(v.name)):
            for old in work.iterdir():
                if old.name != ".git":
                    shutil.rmtree(old) if old.is_dir() else old.unlink()
            shutil.copytree(version, work, dirs_exist_ok=True)
            # Same-sized files written in the same second look unchanged
            # to git's index: drop it, so that every file is read again.
            (work / ".git" / "index").unlink(missing_ok=True)
            git("add", "--all")
            git("commit", "--quiet", "--no-gpg-sign", "-m", f"v{version.name}")
            git("tag", f"v{version.name}")
        (work / ".stamp").write_text(stamp)
        if target.exists():
            shutil.rmtree(target, ignore_errors=True)
        try:
            work.rename(target)
        except OSError:   # another run made it first
            shutil.rmtree(work, ignore_errors=True)


def package_checksum(top: Path) -> str:
    """The checksum of a package's files, as `driver.lock.checksum` computes
    it: each file, in name order directory by directory, as its path, a NUL,
    its size, a NUL and its bytes."""
    h = hashlib.sha256()

    def add(rel: str) -> None:
        for name in sorted(os.listdir(top / rel if rel else top)):
            if not rel and name == ".git":
                continue
            sub = f"{rel}/{name}" if rel else name
            if (top / sub).is_dir():
                add(sub)
            else:
                data = (top / sub).read_bytes()
                h.update(f"{sub}\0{len(data)}\0".encode())
                h.update(data)
    add("")
    return h.hexdigest()


def make_registry_fixtures(sources: Path, dest: Path) -> None:
    """Builds the registry that registry dependency tests use: each
    directory `tests/registry/<name>/<X.Y.Z>/` is an archive
    `build/registry-fixtures/archives/<name>-<X.Y.Z>.tar.gz` and a line of
    the file of `<name>` in the index, `build/registry-fixtures/index`. The
    versions listed in `tests/registry/yanked` (`name X.Y.Z`) are yanked."""
    if not sources.is_dir():
        return
    files = sorted(f for f in sources.rglob("*") if f.is_file())
    stamp = json.dumps([[f.relative_to(sources).as_posix(), f.read_text()] for f in files])
    if (dest / ".stamp").exists() and (dest / ".stamp").read_text() == stamp:
        return
    work = Path(tempfile.mkdtemp(prefix="registry-", dir=dest.parent))
    yanked_file = sources / "yanked"
    yanked = set(yanked_file.read_text().split("\n")) if yanked_file.exists() else set()
    (work / "archives").mkdir()
    index = work / "index"
    index.mkdir()
    download = (dest / "archives").as_posix() + "/{name}-{version}.tar.gz"
    config = json.dumps({"download": "file://" + download}) + "\n"
    (index / "config.json").write_text(config, newline="\n")
    for package in sorted(p for p in sources.iterdir() if p.is_dir()):
        name = package.name
        lines = []
        for version in sorted((v for v in package.iterdir() if v.is_dir()),
                              key=lambda v: git_version_key(v.name)):
            archive = work / "archives" / f"{name}-{version.name}.tar.gz"
            with tarfile.open(archive, "w:gz") as tar:
                for f in sorted(f for f in version.rglob("*") if f.is_file()):
                    tar.add(f, arcname=f.relative_to(version).as_posix())
            lines.append(json.dumps({"version": version.name, "dependencies": {},
                                     "checksum": package_checksum(version),
                                     "yanked": f"{name} {version.name}" in yanked}))
        n = len(name)
        rel = (f"{n}/{name}" if n <= 2 else f"3/{name[0]}/{name}" if n == 3
               else f"{name[0:2]}/{name[2:4]}/{name}")
        (index / rel).parent.mkdir(parents=True, exist_ok=True)
        (index / rel).write_text("\n".join(lines) + "\n", newline="\n")
    env = {**os.environ, "GIT_AUTHOR_NAME": "ofir", "GIT_AUTHOR_EMAIL": "ofir@example.com",
           "GIT_COMMITTER_NAME": "ofir", "GIT_COMMITTER_EMAIL": "ofir@example.com",
           "GIT_CONFIG_GLOBAL": os.devnull}
    for args in (["init", "--quiet", "--initial-branch=main"], ["add", "--all"],
                 ["commit", "--quiet", "--no-gpg-sign", "-m", "index"]):
        subprocess.run(["git", "-C", str(index), *args], env=env, check=True,
                       capture_output=True)
    (work / ".stamp").write_text(stamp)
    if dest.exists():
        shutil.rmtree(dest, ignore_errors=True)
    try:
        work.rename(dest)
    except OSError:   # another run made it first
        shutil.rmtree(work, ignore_errors=True)


def compiler_cmd(cfg: Config, *args: str) -> list[str]:
    # A compiler given as a .py file (used by the runner self-test) runs
    # through the current interpreter.
    if cfg.compiler.endswith(".py"):
        return [sys.executable, cfg.compiler, *args]
    return [cfg.compiler, *args]


def describe(proc: subprocess.CompletedProcess) -> str:
    return (f"exit status {proc.returncode}\n"
            f"--- stdout ---\n{proc.stdout}--- stderr ---\n{proc.stderr}")


def compare_output(cfg: Config, path: Path, actual: str, expected_suffix: str) -> Result:
    expected_path = path.with_suffix(expected_suffix)
    if cfg.bless:
        expected_path.write_text(actual)
        return Result(path, "pass", "blessed")
    if not expected_path.exists():
        return Result(path, "fail", f"missing {expected_path.name} (run with --bless to create it)")
    expected = expected_path.read_text()
    if actual == expected:
        return Result(path, "pass")
    path.with_suffix(".actual").write_text(actual)
    return Result(path, "fail", f"output differs from {expected_path.name}; actual output saved to "
                                f"{path.with_suffix('.actual').name}")


def parse_annotations(source: str, file: str = "") -> list[tuple[int, Diagnostic]]:
    """Returns (function_start_line, diagnostic) for each annotation. The
    `ofir.json` of a project test is annotated with the same comments."""
    annotations = []
    current_fn = 0
    for number, text in enumerate(source.splitlines(), start=1):
        if TOP_LEVEL_FN_RE.match(text):
            current_fn = number
        m = ANNOTATION_RE.search(text)
        if m:
            annotations.append((current_fn, Diagnostic(number, m.group(1), m.group(2), m.group(3),
                                                       file)))
    return annotations


def parse_diagnostics(output: str) -> list[Diagnostic]:
    diagnostics = []
    for text in output.splitlines():
        m = DIAGNOSTIC_RE.match(text)
        if m:
            file = "ofir.json" if m.group(1).endswith("ofir.json") else ""
            diagnostics.append(Diagnostic(int(m.group(2)), m.group(4), m.group(5), m.group(6),
                                          file))
    return diagnostics


def unexpected_diagnostics(reported, annotations) -> list[str]:
    """Every reported error must be annotated (both stages); every warning
    too, in a test that annotates warnings. A test of errors is not about
    the unused variables it needs to trigger them."""
    annotated = {(d.file, d.line, d.code) for _, d in annotations}
    checks_warnings = any(d.severity == "warning" for _, d in annotations)
    return [f"unexpected {d.severity}[{d.code}] at line {d.line}: {d.message}"
            for d in reported if (d.file, d.line, d.code) not in annotated
            and (d.severity == "error" or checks_warnings)]


def missing_stage0(reported, annotations) -> list[str]:
    """Stage 0 reports only the first error of each function and its messages
    are terse: require the first annotation of each function."""
    reported_keys = {(d.file, d.line, d.code) for d in reported}
    firsts: dict[int, Diagnostic] = {}
    for fn_line, d in annotations:
        firsts.setdefault(fn_line, d)
    return [f"missing {d.severity}[{d.code}] at line {d.line}"
            for d in firsts.values() if (d.file, d.line, d.code) not in reported_keys]


def missing_stage1(reported, annotations) -> list[str]:
    """Stage 1 must report every annotation, with a matching message."""
    problems = []
    for _, d in annotations:
        matches = [r for r in reported if (r.file, r.line, r.code) == (d.file, d.line, d.code)]
        if not matches:
            problems.append(f"missing {d.severity}[{d.code}] at line {d.line}: {d.message}")
        elif d.message and not any(d.message in r.message for r in matches):
            problems.append(f"message mismatch at line {d.line}: expected '{d.message}', "
                            f"got '{matches[0].message}'")
    return problems


def write_lock(cfg: Config, template: Path) -> None:
    """Writes the `ofir.lock` of a test from its `ofir.lock.in`, where
    `{fixtures}` stands for the absolute path of the git fixtures and
    `{registry}` for that of the registry's index."""
    build = Path(__file__).resolve().parents[2] / "build"
    text = template.read_text().replace("{fixtures}", (build / "git-fixtures").as_posix())
    text = text.replace("{registry}", (build / "registry-fixtures" / "index").as_posix())
    (template.parent / "ofir.lock").write_text(text, newline="\n")


def parse_notes(source: str) -> list[tuple[int, str, str]]:
    """Returns (line, code, note) for each `// note:` line: a note of the
    diagnostic annotated last."""
    notes = []
    last = None
    for number, text in enumerate(source.splitlines(), start=1):
        m = ANNOTATION_RE.search(text)
        if m:
            last = (number, m.group(2))
            continue
        n = NOTE_RE.match(text)
        if n and last:
            notes.append((last[0], last[1], n.group(1)))
    return notes


def missing_notes(cfg: Config, path: Path, source: str) -> list[str]:
    """Stage 1 must give each annotated note, with the test's path shortened
    to its file name."""
    notes = parse_notes(source)
    if not notes or cfg.stage == 0:
        return []
    proc = run_process(compiler_cmd(cfg, "build", str(path), "--message-format=json",
                                    "-o", str(cfg.tmp / path.stem), *test_flags(source)),
                       env=test_env(cfg, path))
    given = []
    for text in (proc.stdout + proc.stderr).splitlines():
        if not text.startswith("{"):
            continue
        d = json.loads(text)
        for note in d.get("notes", []):
            # Any spelling of the test's path, with backslashes too, ends in its name.
            message = re.sub(r"[^\s`]*[\\/]" + re.escape(path.name), path.name,
                             note["message"])
            given.append((d["line"], d["code"], message))
    problems = []
    for line, code, note in notes:
        if not any((g[0], g[1]) == (line, code) and note in g[2] for g in given):
            problems.append(f"missing note of {code} at line {line}: {note}")
    return problems


def offset_of(data: bytes, line: int, column: int) -> int:
    """The byte offset of a 1-based line and byte column."""
    start = 0
    for _ in range(line - 1):
        start = data.index(b"\n", start) + 1
    return start + column - 1


def wrong_fixes(cfg: Config, path: Path, source: str) -> list[str]:
    """With a `.fixed` file next to the test, stage 1 applies the fixes of its
    help lines (`fix` in JSON) to the test: the result is the `.fixed` text and
    it compiles."""
    expected_path = path.with_suffix(".fixed")
    if cfg.stage == 0 or not expected_path.exists():
        return []
    proc = run_process(compiler_cmd(cfg, "build", str(path), "--message-format=json",
                                    "-o", str(cfg.tmp / path.stem), *test_flags(source)),
                       env=test_env(cfg, path))
    data = path.read_bytes()
    edits = set()
    for text in (proc.stdout + proc.stderr).splitlines():
        if not text.startswith("{"):
            continue
        d = json.loads(text)
        if Path(d.get("file", "")).name != path.name:
            continue
        for note in d.get("notes", []):
            fix = note.get("fix")
            if fix:
                start = offset_of(data, fix["line"], fix["column"])
                end = offset_of(data, fix["endLine"], fix["endColumn"])
                edits.add((start, end, fix["text"].encode()))
    for start, end, text in sorted(edits, reverse=True):
        data = data[:start] + text + data[end:]
    fixed = data.decode(errors="replace")
    if fixed != expected_path.read_text():
        path.with_suffix(".actual").write_text(fixed)
        return [f"the fixes do not give {expected_path.name}; see {path.stem}.actual"]
    out = cfg.tmp / "fixed" / path.name
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    exe = cfg.tmp / "fixed" / path.stem
    built = run_process(compiler_cmd(cfg, "build", str(out), "-o", str(exe), *test_flags(source)),
                        env=test_env(cfg, path))
    if built.returncode != 0:
        return ["the fixed program does not compile\n" + describe(built)]
    return []


def check_diagnostics(cfg: Config, path: Path, source: str) -> Result:
    annotations = parse_annotations(source)
    manifest = path.parent / "ofir.json"
    if path.name == "main.of" and manifest.exists():
        annotations += parse_annotations(manifest.read_text(), "ofir.json")
    template = path.parent / "ofir.lock.in"
    if path.name == "main.of" and template.exists():
        write_lock(cfg, template)
    proc = run_process(compiler_cmd(cfg, "build", str(path), "--message-format=short",
                                    "-o", str(cfg.tmp / path.stem), *test_flags(source)),
                       env=test_env(cfg, path))
    reported = parse_diagnostics(proc.stdout + proc.stderr)
    expects_error = any(d.severity == "error" for _, d in annotations)

    if expects_error and proc.returncode == 0:
        return Result(path, "fail", "expected compilation to fail\n" + describe(proc))
    if not expects_error and proc.returncode != 0:
        return Result(path, "fail", "expected compilation to succeed\n" + describe(proc))

    missing = missing_stage0 if cfg.stage == 0 else missing_stage1
    problems = unexpected_diagnostics(reported, annotations) + missing(reported, annotations)
    problems += missing_notes(cfg, path, source)
    problems += wrong_fixes(cfg, path, source)
    if problems:
        return Result(path, "fail", "\n".join(problems))
    return Result(path, "pass")


def check_panic(cfg: Config, path: Path, source: str) -> Result:
    m = PANIC_RE.search(source)
    if not m:
        return Result(path, "fail", "missing `// panic: message` header")
    proc = run_process(compiler_cmd(cfg, "run", str(path), *test_flags(source)))
    if proc.returncode != PANIC_EXIT_STATUS:
        return Result(path, "fail", f"expected exit status {PANIC_EXIT_STATUS}\n" + describe(proc))
    if f"panic: {m.group(1)}" not in proc.stderr:
        return Result(path, "fail", f"expected 'panic: {m.group(1)}' on stderr\n" + describe(proc))
    # However many threads panic, one message is printed.
    if proc.stderr.count("panic: ") != 1:
        return Result(path, "fail", "expected exactly one panic message\n" + describe(proc))
    return Result(path, "pass")


def expected_status(source: str) -> set[int] | None:
    m = STATUS_RE.search(source)
    if not m:
        return None
    if m.group(1) == "abort":
        return {-signal.SIGABRT, 128 + signal.SIGABRT, 3, 0xC0000409}
    return {int(m.group(1))}


def run_built(cfg: Config, path: Path, source: str,
              statuses: set[int]) -> Result:
    """Builds a run test and runs the program itself, so that its own exit
    status, a signal included, is seen rather than the driver's."""
    rel = path.relative_to(cfg.tests_dir).with_suffix("").as_posix().replace("/", "_")
    exe = cfg.tmp / "status" / rel
    exe.parent.mkdir(parents=True, exist_ok=True)
    env, error = build_c_fixtures(cfg, path, source)
    if error:
        return Result(path, "fail", error)
    proc = run_process(compiler_cmd(cfg, "build", str(path), "--deny-warnings", "-o", str(exe),
                                    *test_flags(source)), env=env)
    if proc.returncode != 0:
        return Result(path, "fail", describe(proc))
    if not exe.exists() and exe.with_suffix(".exe").exists():
        exe = exe.with_suffix(".exe")
    proc = run_process([str(exe)], env=test_env(cfg, path))
    if proc.returncode not in statuses:
        wanted = " or ".join(str(s) for s in sorted(statuses))
        return Result(path, "fail", f"expected exit status {wanted}\n" + describe(proc))
    return compare_output(cfg, path, proc.stdout, ".out")


def test_flags(source: str) -> list[str]:
    return [flag for line in FLAGS_RE.findall(source) for flag in line.split()]


def run_consumer(cfg: Config, path: Path, source: str, consumer: str) -> Result:
    """Builds the test as a library, then a C or C++ program that includes
    its generated header and links it, and runs that program; a Python
    consumer loads the library instead. A `.h` file next to the test is the
    header it expects."""
    rel = path.relative_to(cfg.tests_dir).with_suffix("").as_posix().replace("/", "_")
    out = cfg.tmp / "lib" / rel
    out.mkdir(parents=True, exist_ok=True)
    flags = test_flags(source)
    shared = "--lib=shared" in flags
    ext = ("dll" if os.name == "nt" else "dylib" if sys.platform == "darwin" else "so")
    lib = out / (f"lib{path.stem}." + (ext if shared else "a"))
    proc = run_process(compiler_cmd(cfg, "build", str(path), "--deny-warnings", "-o", str(lib),
                                    *flags), env=test_env(cfg, path))
    if proc.returncode != 0:
        return Result(path, "fail", describe(proc))
    header = out / f"{path.stem}.h"
    expected = path.with_suffix(".h")
    if expected.exists():
        result = compare_output(cfg, path, header.read_text(), ".h")
        if result.status != "pass":
            return result
    if consumer.endswith(".py"):   # loads the shared library with ctypes (ADR 0070)
        proc = run_process([sys.executable, "-I", str(path.parent / consumer), str(lib)],
                           env=test_env(cfg, path))
        if proc.returncode != 0:
            return Result(path, "fail", f"{consumer} failed\n" + describe(proc))
        return compare_output(cfg, path, proc.stdout, ".out")
    root = Path(__file__).resolve().parents[2]
    runtime = os.environ.get("OFIR_RUNTIME") or str(root / "build" / "libofir_rt.a")
    clang, _ = c_tools()
    exe = out / "consumer"
    std = "-std=c11"
    if consumer.endswith(".cpp"):   # the same header and library from C++
        clang = str(Path(clang).with_name(Path(clang).name.replace("clang", "clang++", 1)))
        std = "-std=c++17"
    args = [clang, std, "-Wall", "-Werror", "-I", str(out), str(path.parent / consumer),
            str(lib)]
    if os.name == "nt":   # the MinGW libraries, as the compiler links them
        args.append("--target=x86_64-w64-windows-gnu")
    if not shared:
        args += [runtime, "-lm", "-lpthread"]
    proc = run_process(args + ["-o", str(exe)])
    if proc.returncode != 0:
        return Result(path, "fail", f"cannot build {consumer}\n" + describe(proc))
    if not exe.exists() and exe.with_suffix(".exe").exists():
        exe = exe.with_suffix(".exe")
    proc = run_process([str(exe)], env=test_env(cfg, path))
    if proc.returncode != 0:
        return Result(path, "fail", f"{consumer} failed\n" + describe(proc))
    return compare_output(cfg, path, proc.stdout, ".out")


def check_run(cfg: Config, path: Path, source: str) -> Result:
    consumer = CONSUMER_RE.search(source)
    if consumer:
        return run_consumer(cfg, path, source, consumer.group(1))
    template = path.parent / "ofir.lock.in"
    if path.name == "main.of" and template.exists():
        write_lock(cfg, template)
    statuses = expected_status(source)
    if cfg.target:
        if statuses is not None or C_FIXTURE_RE.search(source):
            return Result(path, "skip", "host only")
        return run_target(cfg, path, source)
    if statuses is not None:
        return run_built(cfg, path, source, statuses)
    env, error = build_c_fixtures(cfg, path, source)
    if error:
        return Result(path, "fail", error)
    # Programs that run are warning-free: a warning fails the test.
    proc = run_process(compiler_cmd(cfg, "run", str(path), "--deny-warnings",
                                    *test_flags(source)), env=env)
    if proc.returncode != 0:
        return Result(path, "fail", describe(proc))
    return compare_output(cfg, path, proc.stdout, ".out")


def run_target(cfg: Config, path: Path, source: str) -> Result:
    """A run test built with `--target` and run under OFIR_RUNNER."""
    proc = run_process(compiler_cmd(cfg, "run", str(path), "--deny-warnings",
                                    f"--target={cfg.target}", *test_flags(source)),
                       env=test_env(cfg, path))
    if proc.returncode != 0:
        return Result(path, "fail", describe(proc))
    return compare_output(cfg, path, proc.stdout, ".out")


def check_emit(cfg: Config, path: Path, what: str, suffix: str) -> Result:
    proc = run_process(compiler_cmd(cfg, "build", str(path), f"--emit={what}"))
    if proc.returncode != 0:
        return Result(path, "fail", describe(proc))
    return compare_output(cfg, path, proc.stdout, suffix)


def check_codegen(cfg: Config, path: Path, source: str) -> Result:
    out = cfg.tmp / path.stem
    proc = run_process(compiler_cmd(cfg, "build", str(path), "--emit=llvm", "-o", str(out),
                                    *test_flags(source)))
    if proc.returncode != 0:
        return Result(path, "fail", describe(proc))
    ir_path = out.with_suffix(".ll")
    if not ir_path.exists():
        return Result(path, "fail", f"compiler did not write {ir_path}")
    ir_lines = ir_path.read_text().splitlines()

    # A small subset of LLVM FileCheck: CHECK patterns must appear in order;
    # CHECK-NOT patterns must not appear between the surrounding CHECK matches.
    position = 0
    pending_not: list[str] = []
    checks = [CHECK_RE.match(line) for line in source.splitlines()]
    for m in filter(None, checks):
        kind, pattern = m.group(1), m.group(2)
        if kind == "CHECK-NOT":
            pending_not.append(pattern)
            continue
        found = next((i for i in range(position, len(ir_lines)) if pattern in ir_lines[i]), None)
        if found is None:
            return Result(path, "fail", f"CHECK not found: {pattern}")
        for banned in pending_not:
            if any(banned in line for line in ir_lines[position:found]):
                return Result(path, "fail", f"CHECK-NOT matched: {banned}")
        pending_not.clear()
        position = found + 1
    for banned in pending_not:
        if any(banned in line for line in ir_lines[position:]):
            return Result(path, "fail", f"CHECK-NOT matched: {banned}")
    return Result(path, "pass")


def check_fmt(cfg: Config, path: Path) -> Result:
    """`ofir fmt --stdout` of the file must match its .fmt file, and
    formatting that output again must not change it."""
    if cfg.stage == 0:
        return Result(path, "skip", "`fmt` is a stage 1 command")
    proc = run_process(compiler_cmd(cfg, "fmt", "--stdout", str(path)))
    if proc.returncode != 0:
        return Result(path, "fail", describe(proc))
    result = compare_output(cfg, path, proc.stdout, ".fmt")
    if result.status != "pass" or cfg.bless:
        return result
    with tempfile.TemporaryDirectory() as tmp:
        again_path = Path(tmp) / path.name
        again_path.write_text(proc.stdout)
        again = run_process(compiler_cmd(cfg, "fmt", "--stdout", str(again_path)))
    if again.returncode != 0 or again.stdout != proc.stdout:
        return Result(path, "fail", "formatting the output again changes it\n" + describe(again))
    return result


def check_doc(cfg: Config, path: Path) -> Result:
    """`ofir doc --stdout` of the file must match its .html file."""
    if cfg.stage == 0:
        return Result(path, "skip", "`doc` is a stage 1 command")
    proc = run_process(compiler_cmd(cfg, "doc", "--stdout", str(path)))
    if proc.returncode != 0:
        return Result(path, "fail", describe(proc))
    return compare_output(cfg, path, proc.stdout, ".html")


# The session of an `lsp` test without a `.lsp` file: open the document.
DEFAULT_LSP_SCRIPT = """\
{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}}
{"jsonrpc": "2.0", "method": "initialized", "params": {}}
{"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": \
{"uri": "$URI", "languageId": "ofir", "version": 1, "text": $TEXT}}}
{"jsonrpc": "2.0", "id": 2, "method": "shutdown"}
{"jsonrpc": "2.0", "method": "exit"}
"""


def check_lsp(cfg: Config, path: Path) -> Result:
    """`ofir lsp` is sent the messages of the test's `.lsp` file, one JSON
    object per line, where `$URI` is the `.of` file's URI, `$DIR` its
    directory's and `$TEXT` its text as a JSON string; its messages, one per
    line with the URI written `$URI`, the directory `$DIR` and the standard
    library's directory `$STD`, must match the `.out` file, and it must exit
    with status 0."""
    if cfg.stage == 0:
        return Result(path, "skip", "`lsp` is a stage 1 command")
    script_path = path.with_suffix(".lsp")
    script = script_path.read_text() if script_path.exists() else DEFAULT_LSP_SCRIPT
    uri = path.resolve().as_uri()
    dir_uri = path.parent.resolve().as_uri()
    std = Path(os.environ.get("OFIR_STD", Path(__file__).resolve().parents[2] / "std"))
    std_uri = std.resolve().as_uri()
    text = json.dumps(path.read_text())
    framed = ""
    for line in script.replace("\\\n", "").splitlines():
        if line.strip():
            body = line.replace("$URI", uri).replace("$DIR", dir_uri).replace("$TEXT", text)
            framed += f"Content-Length: {len(body.encode())}\r\n\r\n{body}"
    proc = run_process(compiler_cmd(cfg, "lsp"), input_text=framed)
    if proc.returncode != 0:
        return Result(path, "fail", describe(proc))
    messages = []
    # Text mode reads each `\r\n` of the headers as `\n`; the bodies, JSON
    # written on one line, are unchanged.
    rest = proc.stdout.encode()
    while rest:
        header, _, rest = rest.partition(b"\n\n")
        length = int(header.split(b":")[1])
        message = rest[:length].decode().replace(uri, "$URI").replace(dir_uri, "$DIR")
        messages.append(message.replace(std_uri, "$STD"))
        rest = rest[length:]
    return compare_output(cfg, path, "".join(m + "\n" for m in messages), ".out")


def run_test(cfg: Config, suite: str, path: Path) -> Result:
    source = path.read_text(errors="replace")   # invalid UTF-8 is a test input
    windows = cfg.target.endswith("-windows-gnu") if cfg.target else os.name == "nt"
    for requirement in REQUIRES_RE.findall(source):
        if requirement == "stage1" and cfg.stage == 0:
            return Result(path, "skip", "requires stage1")
        if requirement == "stage0" and cfg.stage == 1:
            return Result(path, "skip", "requires stage0")
        if requirement == "unix" and windows:   # of the target system
            return Result(path, "skip", "requires unix")
        if requirement == "windows" and not windows:
            return Result(path, "skip", "requires windows")
        if requirement == "host" and cfg.target:
            return Result(path, "skip", "requires host")
    if suite == "lex":
        return check_emit(cfg, path, "tokens", ".tokens")
    if suite == "parse":
        return check_emit(cfg, path, "ast", ".ast")
    if suite == "run":
        return check_run(cfg, path, source)
    if suite == "panic":
        return check_panic(cfg, path, source)
    if suite == "codegen":
        return check_codegen(cfg, path, source)
    if suite == "fmt":
        return check_fmt(cfg, path)
    if suite == "doc":
        return check_doc(cfg, path)
    if suite == "lsp":
        return check_lsp(cfg, path)
    # compile_fail and borrow
    return check_diagnostics(cfg, path, source)


def is_module_of_project(path: Path, suite_dir: Path) -> bool:
    """A directory with a main.of is one multi-file test: its other files are
    modules of that program, not tests."""
    for parent in path.parents:
        if parent == suite_dir or suite_dir not in parent.parents:
            break
        if (parent / "main.of").exists() and path != parent / "main.of":
            return True
    return False


def discover(tests_dir: Path, filters: list[str]) -> list[tuple[str, Path]]:
    found = []
    for suite in SUITES:
        suite_dir = tests_dir / suite
        for path in sorted(suite_dir.rglob("*.of")):
            if is_module_of_project(path, suite_dir):
                continue
            rel = path.relative_to(tests_dir).as_posix()
            if not filters or any(f in rel for f in filters):
                found.append((suite, path))
    return found


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("filters", nargs="*",
                        help="run only tests whose path contains one of these")
    parser.add_argument("--compiler", type=executable,
                        default=str(root / "build" / "stage0" / "ofir"))
    parser.add_argument("--stage", type=int, choices=[0, 1],
                        help="compiler stage (default: 0 if the path contains 'stage0')")
    parser.add_argument("--tests-dir", type=Path, default=root / "tests")
    parser.add_argument("--bless", action="store_true", help="overwrite expected output files")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("-v", "--verbose", action="store_true", help="also list passing tests")
    parser.add_argument("--target", default="",
                        help="build run tests for this triple and run them under OFIR_RUNNER")
    args = parser.parse_args(argv)

    stage = args.stage if args.stage is not None else (0 if "stage0" in args.compiler else 1)
    cfg = Config(args.compiler, stage, args.bless, args.tests_dir.resolve(), args.verbose,
                 args.target)

    tests = discover(cfg.tests_dir, args.filters)
    if not tests:
        print("no tests found")
        return 0
    if not Path(cfg.compiler).exists():
        print(f"error: compiler not found: {cfg.compiler}", file=sys.stderr)
        return 2

    fixtures = root / "build" / "git-fixtures"
    fixtures.mkdir(parents=True, exist_ok=True)
    make_git_fixtures(cfg.tests_dir / "git", fixtures)
    make_registry_fixtures(cfg.tests_dir / "registry", root / "build" / "registry-fixtures")

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda t: run_test(cfg, *t), tests))

    counts = {"pass": 0, "fail": 0, "skip": 0}
    for r in results:
        counts[r.status] += 1
        rel = r.path.relative_to(cfg.tests_dir).as_posix()
        if r.status == "fail":
            print(f"FAIL {rel}\n" + "\n".join("     " + line for line in r.detail.splitlines()))
        elif cfg.verbose:
            print(f"{r.status.upper():4} {rel}" + (f" ({r.detail})" if r.detail else ""))
    print(f"\n{len(results)} tests: {counts['pass']} passed, {counts['fail']} failed, "
          f"{counts['skip']} skipped (stage {stage})")
    return 1 if counts["fail"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
