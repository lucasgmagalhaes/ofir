#!/usr/bin/env python3
"""Checks the release archive and its installer (ADR 0082, OFIR-69; ADR 0084, OFIR-71).

- `tools/dist.py` gives the same bytes twice, and the `.sha256` next to
  the archive matches it;
- `install.sh` installs it into an empty prefix, and the installed
  compiler builds and runs a program, and explains an error, with
  `OFIR_STD` and `OFIR_RUNTIME` unset and the repository out of reach,
  and runs the compatibility corpus (`tools/check_compat.py`, ADR 0084);
- a second version installs next to the first and becomes active, and
  `--use` rolls back to the first;
- a manifest's `toolchain` that the compiler is not in is E0923, whose
  help names the installed version in the range, and an invalid one is
  E0910;
- an archive with a wrong checksum is rejected with nothing changed;
- `--remove` refuses the active version, and `--uninstall` leaves the
  prefix as it was before.

    python3 tools/check_install.py --compiler build/stage1/ofir --runtime build/libofir_rt.a
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PROGRAM = """fn main() {
    println("installed")
}
"""


def fail(message):
    print(f"install: {message}", file=sys.stderr)
    sys.exit(1)


def run(cmd, cwd, env, ok=True):
    out = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    if ok and out.returncode != 0:
        fail(f"`{' '.join(map(str, cmd))}` exited with {out.returncode}:\n"
             f"{out.stdout}{out.stderr}")
    return out


def files(prefix):
    return sorted(p.relative_to(prefix).as_posix() for p in prefix.rglob("*"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    parser.add_argument("--runtime", default=os.environ.get("OFIR_RUNTIME", "build/libofir_rt.a"))
    args = parser.parse_args()
    if shutil.which("sh") is None or shutil.which("tar") is None:
        print("install: skipped (needs sh and tar)")
        return
    compiler = Path(args.compiler).resolve()
    runtime = Path(args.runtime).resolve()
    version = re.search(r'"([^"]+)"', (ROOT / "compiler/driver/version.of").read_text()).group(1)
    major, minor, _ = (int(n) for n in version.split("."))
    other = f"{major}.{minor + 1}.0"
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        dist = tmp / "dist"
        build = [sys.executable, str(ROOT / "tools/dist.py"), "--compiler", str(compiler),
                 "--runtime", str(runtime)]
        env = dict(os.environ, SOURCE_DATE_EPOCH="1700000000")
        run(build + ["--out", str(dist)], tmp, env)
        run(build + ["--out", str(tmp / "again")], tmp, env)
        archives = sorted(dist.glob("*.tar.gz"))
        if len(archives) != 1:
            fail(f"dist.py wrote {len(archives)} archives")
        archive = archives[0]
        if archive.read_bytes() != (tmp / "again" / archive.name).read_bytes():
            fail("dist.py gave different bytes for the same inputs")
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if (dist / f"{archive.name}.sha256").read_text() != f"{digest}  {archive.name}\n":
            fail("the .sha256 does not match the archive")
        run(build + ["--out", str(dist), "--version", other], tmp, env)
        second = dist / archive.name.replace(f"ofir-{version}-", f"ofir-{other}-")

        # A clean machine: no repository, no OFIR_STD or OFIR_RUNTIME.
        clean = {k: v for k, v in os.environ.items() if k not in ("OFIR_STD", "OFIR_RUNTIME")}
        clean["OFIR_HOME"] = str(tmp / "home")
        prefix = tmp / "prefix"
        prefix.mkdir()
        (prefix / "notes.txt").write_text("mine\n")
        before = files(prefix)
        work = tmp / "work"
        work.mkdir()
        (work / "main.of").write_text(PROGRAM)
        installer = tmp / "install.sh"
        shutil.copy(ROOT / "tools/install.sh", installer)
        sh = ["sh", installer.as_posix(), "--prefix", prefix.as_posix()]

        run(sh + [archive.as_posix()], work, clean)
        exe = prefix / "bin" / compiler.name
        out = run([str(exe), "run", "main.of"], work, clean)
        if out.stdout != "installed\n":
            fail(f"the installed compiler printed {out.stdout!r}")
        out = run([str(exe), "explain", "E0923"], work, clean)
        if "another toolchain" not in out.stdout:
            fail("`ofir explain` does not find the installed explanations")
        out = run([str(exe), "version"], work, clean)
        if f"ofir {version}" not in out.stdout or "prefix: " not in out.stdout:
            fail(f"`ofir version` printed {out.stdout!r}")
        # The installed compiler runs the compatibility corpus (ADR 0084).
        out = run([sys.executable, str(ROOT / "tools/check_compat.py"), "--installed",
                   str(prefix)], work, clean, ok=False)
        if out.returncode != 0:
            fail(f"the installed compiler fails the compatibility corpus:\n{out.stderr}")

        run(sh + [second.as_posix()], work, clean)
        if (prefix / "share/ofir/version").read_text() != f"{other}\n":
            fail("the second version is not active")
        out = run(sh + ["--list"], work, clean)
        if out.stdout != f"{version}\n{other} (active)\n":
            fail(f"--list printed {out.stdout!r}")
        run(sh + ["--use", version], work, clean)
        if (prefix / "share/ofir/version").read_text() != f"{version}\n":
            fail("--use did not roll back")
        out = run([str(exe), "run", "main.of"], work, clean)
        if out.stdout != "installed\n":
            fail("the rolled back compiler does not run a program")

        # A pinned toolchain.
        project = tmp / "project"
        (project / "src").mkdir(parents=True)
        (project / "src/main.of").write_text(PROGRAM)
        manifest = '{"name": "app", "version": "0.1.0", "toolchain": "%s"}\n'
        (project / "ofir.json").write_text(manifest % version)
        out = run([str(exe), "run", "src/main.of"], project, clean)
        if out.stdout != "installed\n":
            fail(f"a project pinned to {version} does not build: {out.stderr}")
        (project / "ofir.json").write_text(manifest % f">={other}")
        out = run([str(exe), "build", "src/main.of"], project, clean, ok=False)
        if out.returncode == 0 or "E0923" not in out.stderr \
                or f"install.sh --use {other}" not in out.stderr:
            fail(f"an unmet toolchain is not E0923 with the version to use:\n{out.stderr}")
        (project / "ofir.json").write_text(manifest % "soon")
        out = run([str(exe), "build", "src/main.of"], project, clean, ok=False)
        if out.returncode == 0 or "E0910" not in out.stderr:
            fail(f"an invalid toolchain is not E0910:\n{out.stderr}")

        # A wrong checksum changes nothing.
        bad = tmp / "bad" / second.name
        bad.parent.mkdir()
        shutil.copy(second, bad)
        Path(f"{bad}.sha256").write_text(f"{'0' * 64}  {bad.name}\n")
        installed = files(prefix)
        out = run(sh + [bad.as_posix()], work, clean, ok=False)
        if out.returncode == 0 or "checksum" not in out.stderr or files(prefix) != installed:
            fail("an archive with a wrong checksum was installed")

        out = run(sh + ["--remove", version], work, clean, ok=False)
        if out.returncode == 0:
            fail("--remove removed the active version")
        run(sh + ["--remove", other], work, clean)
        run(sh + ["--uninstall"], work, clean)
        if files(prefix) != before:
            fail(f"--uninstall left {sorted(set(files(prefix)) - set(before))}")
    print("install: archive, install, rollback, pinned toolchain and uninstall work")


if __name__ == "__main__":
    main()
