#!/usr/bin/env python3
"""Checks the command line of `ofir fmt` (docs/design/toolchain.md):

- with no paths it formats the `.of` files under the current directory,
  leaving out hidden entries and other files;
- `--check` lists the files that would change and exits with 1, without
  writing them; `--stdout` prints the formatted text;
- a file that does not lex or parse is reported and gives 1;
- an unknown option or a path that does not exist gives 2, and a file
  that cannot be read or written gives 1.

    python3 tools/check_fmt_cli.py --compiler build/stage1/ofir
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

MESSY = "fn main() {\n  print(\"hi\")\n}\n"
TIDY = "fn main() {\n    print(\"hi\")\n}\n"


def fail(message: str) -> None:
    print(f"fmt-cli: {message}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)

        def fmt(*flags: str, status: int, where: Path = work) -> subprocess.CompletedProcess:
            result = subprocess.run([compiler, "fmt", *flags], cwd=where,
                                    capture_output=True, text=True, errors="replace",
                                    timeout=300)
            if result.returncode != status:
                fail(f"fmt {' '.join(flags)} exited with {result.returncode}, not {status}:\n"
                     f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
            return result

        tree = work / "tree"
        (tree / "sub").mkdir(parents=True)
        (tree / ".hidden").mkdir()
        (tree / "messy.of").write_text(MESSY, newline="\n")
        (tree / "tidy.of").write_text(TIDY, newline="\n")
        (tree / "sub" / "deep.of").write_text(MESSY, newline="\n")
        (tree / ".hidden" / "skipped.of").write_text(MESSY, newline="\n")
        (tree / "notes.txt").write_text(MESSY, newline="\n")

        listed = fmt("--check", status=1, where=tree).stdout.split()
        if sorted(listed) != ["./messy.of", "./sub/deep.of"]:
            fail(f"--check listed {listed}")
        if (tree / "messy.of").read_text() != MESSY:
            fail("--check wrote a file")
        printed = fmt("--stdout", "messy.of", status=0, where=tree).stdout
        if printed != TIDY or (tree / "messy.of").read_text() != MESSY:
            fail(f"--stdout printed {printed!r} or wrote the file")
        fmt(status=0, where=tree)
        for name in ("messy.of", "sub/deep.of"):
            if (tree / name).read_text() != TIDY:
                fail(f"{name} was not formatted")
        if (tree / ".hidden" / "skipped.of").read_text() != MESSY:
            fail("a hidden directory was formatted")
        fmt("--check", str(tree), status=0)

        unlexed = work / "unlexed.of"
        unlexed.write_text("fn main() {\n    print(\"hi)\n}\n")
        if "error" not in fmt(str(unlexed), status=1).stderr:
            fail("a file that does not lex is not reported")
        unparsed = work / "unparsed.of"
        unparsed.write_text("fn main( {\n}\n")
        if "error" not in fmt(str(unparsed), status=1).stderr:
            fail("a file that does not parse is not reported")

        if "unknown option `--wide`" not in fmt("--wide", status=2).stderr:
            fail("an unknown option is not reported")
        if "cannot read" not in fmt(str(work / "missing.of"), status=2).stderr:
            fail("a missing path is not reported")
        if Path("/proc/self/mem").exists():   # Linux: reading it fails at the first byte
            unreadable = work / "memory.of"
            unreadable.symlink_to("/proc/self/mem")
            if "cannot read" not in fmt(str(unreadable), status=1).stderr:
                fail("a file that cannot be read is not reported")
        if hasattr(os, "geteuid") and os.geteuid() != 0:   # root writes a read-only file
            locked = work / "locked.of"
            locked.write_text(MESSY, newline="\n")
            locked.chmod(0o444)
            if "cannot write" not in fmt(str(locked), status=1).stderr:
                fail("a file that cannot be written is not reported")
    print("fmt-cli: ok")


if __name__ == "__main__":
    main()
