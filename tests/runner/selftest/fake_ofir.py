#!/usr/bin/env python3
"""A fake compiler for the runner self-test.

It does not compile anything: it reads `// fake-...:` directives from the
test file and behaves as they say.

    // fake-stdout: TEXT          print TEXT (one line per directive)
    // fake-diag: LINE CODE MSG   report a diagnostic (E... errors, W... warnings)
    // fake-panic: MSG            print `panic: MSG` and exit with 101
    // fake-ir: TEXT              write TEXT as a line of OUT.ll (--emit=llvm)
    // fake-flaky:                fail the first time, pass afterwards (it
                                  leaves FILE.tried next to the test)
"""

import re
import sys
from pathlib import Path


def main(argv):
    command, path = argv[0], Path(argv[1])
    out = argv[argv.index("-o") + 1] if "-o" in argv else None
    source = path.read_text()

    def directives(name):
        return re.findall(rf"//\s*fake-{name}:\s*(.*?)\s*$", source, re.MULTILINE)

    tried = path.with_name(path.name + ".tried")
    if "fake-flaky:" in source and not tried.exists():
        tried.write_text("")
        print(f"{path}:1:1: error[E0001]: flaky failure", file=sys.stderr)
        return 1
    failed = False
    for diag in directives("diag"):
        line, code, message = diag.split(" ", 2)
        severity = "error" if code.startswith("E") else "warning"
        failed |= severity == "error"
        print(f"{path}:{line}:1: {severity}[{code}]: {message}", file=sys.stderr)
    if failed:
        return 1
    if "--emit=llvm" in argv:
        Path(out + ".ll").write_text("\n".join(directives("ir")) + "\n")
        return 0
    for text in directives("stdout"):
        print(text)
    if command == "run":
        for message in directives("panic"):
            print(f"panic: {message}", file=sys.stderr)
            return 101
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
