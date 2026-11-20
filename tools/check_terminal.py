#!/usr/bin/env python3
"""Checks `std.io.terminalSize` on a pseudo-terminal (ADR 0099, OFIR-83).

A program run with its standard streams on a pseudo-terminal of 100
columns and 30 rows prints `100x30` for each stream, and `none` for a
stream that is a pipe. POSIX only: Windows is skipped.

    python3 tools/check_terminal.py --compiler build/stage1/ofir
"""

import argparse
import os
import select
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PROGRAM = """\
import std.io

fn show(stream: io.Stream) str {
    return match io.terminalSize(stream) {
        Option.Some(size) => "{size.columns}x{size.rows}",
        Option.None => "none"
    }
}

fn main() {
    println("{show(io.Stream.Stdin)} {show(io.Stream.Stdout)} {show(io.Stream.Stderr)}")
}
"""


def fail(message):
    print(f"terminal: {message}", file=sys.stderr)
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    if os.name == "nt":
        print("terminal: skipped (needs a POSIX pseudo-terminal)")
        return
    import fcntl
    import pty
    import struct
    import termios
    compiler = str(Path(args.compiler).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "size.of"
        source.write_text(PROGRAM)
        exe = Path(tmp) / "size"
        build = subprocess.run([compiler, "build", str(source), "-o", str(exe)],
                               capture_output=True, text=True, timeout=600)
        if build.returncode != 0:
            fail(f"the program does not build:\n{build.stdout}{build.stderr}")
        main_fd, sub_fd = pty.openpty()
        fcntl.ioctl(sub_fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
        # The parent keeps the subsidiary end open until it has read the
        # line: macOS discards unread output when its last descriptor closes.
        run = subprocess.Popen([str(exe)], stdin=sub_fd, stdout=sub_fd,
                               stderr=subprocess.PIPE)
        out = b""
        deadline = time.monotonic() + 60
        while b"\n" not in out and time.monotonic() < deadline:
            ready, _, _ = select.select([main_fd], [], [], 0.1)
            if ready:
                out += os.read(main_fd, 4096)
            elif run.poll() is not None and not out:
                break
        run.wait(timeout=60)
        os.close(sub_fd)
        os.close(main_fd)
        if run.returncode != 0:
            fail(f"the program exited with {run.returncode}")
        line = out.decode().strip()
        if line != "100x30 100x30 none":
            fail(f"the program printed {line!r}, not '100x30 100x30 none'")
    print("terminal: ok")


if __name__ == "__main__":
    main()
