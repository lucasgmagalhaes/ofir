#!/usr/bin/env python3
"""Checks the layout rule `"fmt": {"inlineBlocks": false}` (docs/style-guide.md,
"Layout"): the body of a function, and the block of a statement (`if`,
`else`, `for`, `unsafe`, a `match` arm, the `else` of `const ... else`), never
shares a line with its braces. An `if` or `unsafe` used as a value may stay
on one line when each of its blocks holds a single expression:
`const n = if a < b { a } else { b }`. `{ ... }` marks a body left out of
an example.

Applies to the Ofir sources of the repository and to the `ofir` code
blocks of the documentation (Markdown files and doc comments).

    python3 tools/check_format.py [--fix]

`--fix` breaks the offending lines (the doc comments of Ofir files are
left to be fixed by hand).
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["std", "tests", "examples", "bench", "compiler"]
FMT_TESTS = ROOT / "tests" / "fmt"
DOCS = ["docs", "README.md", "bench/README.md", "std/README.md"]

STATEMENT_START = re.compile(
    r"^(pub\s+)?(unsafe\s+)?fn\b|^(if|for|unsafe|match|else)\b|^\}\s*else\b")
ASSIGNMENT = r"(^|[^=!<>+\-*/%&|^])(=|\+=|-=|\*=|/=|%=|<<=|>>=|&=|\|=|\^=)(?!=)"
STATEMENT_IN_BLOCK = re.compile(r"\b(return|break|continue|const|let)\b|" + ASSIGNMENT)


class Stripper:
    """Blanks out comments and string contents, keeping every column, so
    that braces are only those of the code. A block comment may span lines."""

    def __init__(self):
        self.comment = False

    def line(self, line: str) -> str:
        self.text, self.i, self.out = line, 0, []
        self.depth = []   # the brace depth inside each open interpolation
        self.string = False
        while self.i < len(line):
            if self.comment:
                self.skip_comment()
            elif self.string:
                self.string_char()
            elif line.startswith("//", self.i):
                break
            else:
                self.code_char()
        return "".join(self.out)

    def emit(self, text: str, width: int = 1):
        self.out.append(text)
        self.i += width

    def skip_comment(self):
        if self.text.startswith("*/", self.i):
            self.comment = False
            self.emit("  ", 2)
        else:
            self.emit(" ")

    def string_char(self):
        c = self.text[self.i]
        if c == "\\":
            self.emit("  ", 2)
        elif c == '"':
            self.string = False
            self.emit('"')
        elif c == "{":   # an interpolation is an expression, not a block
            self.string = False
            self.depth.append(0)
            self.emit("(")
        else:
            self.emit(" ")

    def code_char(self):
        c = self.text[self.i]
        if self.text.startswith("/*", self.i):
            self.comment = True
            self.emit("  ", 2)
        elif c == "'":   # a byte literal: b'{'
            escaped = self.text[self.i + 1:self.i + 2] == "\\"
            close = self.text.find("'", self.i + (2 if escaped else 1))
            width = (len(self.text) if close < 0 else close + 1) - self.i
            self.emit(" " * width, width)
        elif c == '"':
            self.string = True
            self.emit('"')
        elif c in "{}" and self.depth:
            self.interpolation_brace(c)
        else:
            self.emit(c)

    def interpolation_brace(self, c: str):
        if c == "{":
            self.depth[-1] += 1
            self.emit(c)
        elif self.depth[-1] == 0:   # the end of the interpolation
            self.depth.pop()
            self.string = True
            self.emit(")")
        else:
            self.depth[-1] -= 1
            self.emit(c)


def inline_blocks(code: str):
    """The (start, end, content) of each `{ ... }` block (an opening brace
    after a space, unlike a struct literal `Point{x: 1}`) closed on the line."""
    stack = []
    for i, c in enumerate(code):
        if c == "{":
            stack.append(i)
        elif c == "}" and stack:
            start = stack.pop()
            if start > 0 and code[start - 1] == " ":
                yield start, i, code[start + 1:i].strip()


def violation(code: str) -> bool:
    return offending_block(code) is not None


def offending_block(code: str):
    """The (start, end) of the first block that breaks the rule, or None."""
    text = code.strip()
    statement = bool(STATEMENT_START.match(text))
    for start, end, content in sorted(inline_blocks(code)):   # the outermost first
        if not content or content == "...":   # `{ ... }`: a body left out of an example
            continue
        before = code[:start].rstrip()
        after = code[end + 1:].strip()
        # In a statement, the body is the block that ends the line or comes
        # before `else`; others are values: `if unsafe { p as u64 } == 0 {`.
        body = statement and (after == "" or after.startswith("else"))
        if body or before.endswith("=>") or STATEMENT_IN_BLOCK.search(content):
            return start, end
    return None


def split_line(line: str, code: str):
    """`head { body }tail` as three lines, the comment kept on the first."""
    start, end = offending_block(code)
    indent = line[:len(line) - len(line.lstrip())]
    comment = line[len(code):] if len(code) < len(line) else ""
    head = line[:start].rstrip() + " {" + ("   " + comment.strip() if comment.strip() else "")
    body = indent + "    " + line[start + 1:end].strip()
    tail = indent + "}" + line[end + 1:len(code)].rstrip()
    return [head, body, tail]


def fix_lines(lines: list) -> list:
    out = []
    pending = list(lines)
    stripper = Stripper()
    while pending:
        line = pending.pop(0)
        saved = stripper.comment
        code = stripper.line(line)
        if offending_block(code) is not None:
            stripper.comment = saved
            pending[0:0] = split_line(line, code)
            continue
        out.append(line)
    return out


def fix_file(path: Path):
    text = path.read_text(errors="surrogateescape")
    lines = text.splitlines()
    if path.suffix == ".of":
        lines = fix_lines(lines)
    else:   # only inside the `ofir` code blocks of Markdown
        out, block = [], None
        for line in lines:
            if block is None and line.strip().startswith("```ofir"):
                block = []
                out.append(line)
            elif block is not None and line.strip().startswith("```"):
                out += fix_lines(block) + [line]
                block = None
            elif block is not None:
                block.append(line)
            else:
                out.append(line)
        lines = out
    new = "\n".join(lines) + ("\n" if text.endswith("\n") else "")
    if new != text:
        path.write_text(new, errors="surrogateescape")


def check_lines(lines, name: str, first: int = 1) -> list:
    problems = []
    stripper = Stripper()
    for n, line in enumerate(lines, first):
        if violation(stripper.line(line)):
            problems.append(f"{name}:{n}: a block on one line: put its body on its own lines")
    return problems


def code_blocks(path: Path):
    """The `ofir` code blocks of a Markdown file or of the doc comments of
    an Ofir file, as (first line number, lines)."""
    lines = path.read_text(errors="surrogateescape").splitlines()
    block, start = None, 0
    for n, line in enumerate(lines, 1):
        text = line.strip()
        if path.suffix == ".of":
            if not text.startswith("*"):
                continue
            text = text[1:].strip() if not text.startswith("*/") else ""
            raw = line.split("*", 1)[1][1:] if "*" in line else ""
        else:
            raw = line
        if block is None and text.startswith("```ofir"):
            block, start = [], n + 1
        elif block is not None and text.startswith("```"):
            yield start, block
            block = None
        elif block is not None:
            block.append(raw)


def main() -> int:
    fix = "--fix" in sys.argv[1:]
    problems = []
    files = sorted(p for d in SOURCES if (ROOT / d).exists() for p in (ROOT / d).rglob("*.of"))
    # The inputs of the `fmt` tests are badly formatted on purpose.
    files = [p for p in files if FMT_TESTS not in p.parents]
    for path in files:
        if fix:
            fix_file(path)
        name = str(path.relative_to(ROOT))
        problems += check_lines(path.read_text(errors="surrogateescape").splitlines(), name)
        for start, block in code_blocks(path):
            problems += check_lines(block, name, start)
    docs = []
    for d in DOCS:
        p = ROOT / d
        docs += [p] if p.is_file() else sorted(p.rglob("*.md")) if p.exists() else []
    for path in docs:
        if fix:
            fix_file(path)
        for start, block in code_blocks(path):
            problems += check_lines(block, str(path.relative_to(ROOT)), start)
    for problem in problems:
        print(f"error: {problem}")
    print(f"format: {len(problems)} errors")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
