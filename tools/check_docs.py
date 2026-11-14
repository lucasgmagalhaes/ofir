#!/usr/bin/env python3
"""Checks the documentation comments of the standard library (ADR 0020).

Every file of std/ (but the `_test.of` files, which hold tests of a module
and declare nothing public) starts with a `/** @module ... */` comment, and every
public declaration has a `/** ... */` comment before it: functions,
methods, structs, enums, constants, public fields and the variants of
public enums. The tags of a function's comment must match its signature:

    @typeParam T  for each type parameter of a generic function or type
    @param name   for each parameter except `self`, in order
    @returns      when the function returns a value
    @safety       for an `unsafe fn`
    @panics       when its body calls `panic`

ADR 0062 adds `@complexity`, `@allocates` and `@platform`. A module comment
outside std/core has an `@example`, and each public function of the
collections (std/collections, List and Map) has `@complexity`.

Unknown tags and tags that do not match are errors. Private declarations
need no comment, but a comment they have is checked the same way.
"""

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["std"]
TAGS = {"@module", "@param", "@typeParam", "@returns", "@panics", "@safety", "@example",
        "@see", "@deprecated", "@complexity", "@allocates", "@platform"}
# The files whose public functions state their complexity (ADR 0062).
COMPLEXITY = ("std/collections/", "std/core/list.of", "std/core/map.of")
FN_RE = re.compile(r"^\s*(pub\s+)?(unsafe\s+)?fn\s+(\w+)\s*(<([^>]*)>)?\s*\((.*)\)\s*([^{]*)")
TYPE_RE = re.compile(r"^\s*(pub\s+)?(copy\s+|ref\s+)?(struct|enum)\s+(\w+)\s*(<([^>]*)>)?")
CONST_RE = re.compile(r"^\s*pub\s+(const|var|type)\s+(\w+)")
FIELD_RE = re.compile(r"^\s*pub\s+(\w+)\s*:")
VARIANT_RE = re.compile(r"^\s*([A-Z]\w*)\s*(\(|=|$)")


@dataclass
class Doc:
    text: str
    line: int
    tags: list = field(default_factory=list)   # (tag, first word, line)


def parse_doc(lines: list, end: int, start: int) -> Doc:
    body = []
    for raw in lines[start:end + 1]:
        text = raw.strip()
        text = re.sub(r"^/\*\*", "", text)
        text = re.sub(r"\*/$", "", text).strip()
        text = re.sub(r"^\*\s?", "", text)
        body.append(text)
    doc = Doc("\n".join(body).strip(), start + 1)
    fenced = False
    for i, text in enumerate(body):
        if text.startswith("```"):
            fenced = not fenced
        if fenced:
            continue   # an attribute in an example is code, not a tag
        for m in re.finditer(r"(?:^|\s)(@\w+)\s*(\S*)", text):
            if m.group(1) in TAGS or text.startswith(m.group(1)):
                doc.tags.append((m.group(1), m.group(2), start + 1 + i))
    return doc


def doc_before(lines: list, index: int):
    """The doc comment that ends just before line `index`, skipping attributes."""
    i = index - 1
    while i >= 0 and lines[i].strip().startswith("@") and not lines[i].strip().startswith("@m"):
        i -= 1
    if i < 0 or not lines[i].rstrip().endswith("*/"):
        return None
    end = i
    while i >= 0 and "/*" not in lines[i]:
        i -= 1
    if i < 0 or "/**" not in lines[i]:
        return None
    return parse_doc(lines, end, i)


def split_params(text: str) -> list:
    params, depth, current = [], 0, ""
    for ch in text:
        depth += ch in "<([" and 1 or 0
        depth -= ch in ">)]" and 1 or 0
        if ch == "," and depth == 0:
            params.append(current)
            current = ""
        else:
            current += ch
    if current.strip():
        params.append(current)
    names = []
    for p in params:
        p = p.strip()
        if re.match(r"^(mut\s+|move\s+)?self$", p):
            continue
        m = re.match(r"^(\w+)\s*:", p)
        if m:
            names.append(m.group(1))
    return names


def body_panics(lines: list, index: int) -> bool:
    depth, started = 0, False
    for line in lines[index:]:
        code = re.sub(r'"(\\.|[^"\\])*"', '""', line.split("//")[0])
        if "panic(" in code:
            return True
        depth += code.count("{") - code.count("}")
        started = started or "{" in code
        if started and depth <= 0:
            return False
    return False


def type_params(text) -> list:
    return [p.split(":")[0].strip() for p in (text or "").split(",") if p.strip()]


@dataclass
class Expected:
    """What the signature of a declaration requires of its comment."""
    params: list = field(default_factory=list)
    type_params: list = field(default_factory=list)
    returns: bool = False
    unsafe: bool = False
    panics: bool = False


def check_tags(problems, path, doc: Doc, want: Expected):
    def report(line, message):
        problems.append(f"{path}:{line}: {message}")

    if not re.sub(r"@\w+.*", "", doc.text).strip() and not doc.text.startswith("@module"):
        report(doc.line, "the comment has no description")
    for tag, _, line in doc.tags:
        if tag not in TAGS:
            report(line, f"unknown tag `{tag}`")
    params = [word for tag, word, _ in doc.tags if tag == "@param"]
    if params != want.params:
        report(doc.line, f"@param tags {params} do not match the parameters {want.params}")
    typed = [word for tag, word, _ in doc.tags if tag == "@typeParam"]
    if typed != want.type_params:
        report(doc.line, f"@typeParam tags {typed} do not match the type parameters "
                         f"{want.type_params}")
    present = {tag for tag, _, _ in doc.tags}
    for tag, needed in (("@returns", want.returns), ("@safety", want.unsafe),
                        ("@panics", want.panics)):
        if tag == "@panics" and not needed:
            continue   # a panic in a callee can be documented too
        if needed and tag not in present:
            report(doc.line, f"missing {tag}")
        if not needed and tag in present:
            report(doc.line, f"{tag} does not apply here")


def split_signature(code: str, start: int):
    """The parameters and the result type of a signature whose `(` is at
    `start`: the parentheses balance, so a tuple result is not a parameter."""
    depth = 0
    for j in range(start, len(code)):
        if code[j] == "(":
            depth += 1
        elif code[j] == ")":
            depth -= 1
            if depth == 0:
                return code[start + 1:j], code[j + 1:].split("{")[0]
    return code[start + 1:], ""


def check_fn(problems, path, lines, i, m):
    doc = doc_before(lines, i)
    public = bool(m.group(1))
    if doc is None:
        if public:
            problems.append(f"{path}:{i + 1}: public function `{m.group(3)}` has no /** */ "
                            "documentation")
        return
    params, ret = split_signature(m.string, m.start(6) - 1)
    ret = re.split(r"\bwhere\b", ret)[0].strip()
    returns = bool(ret) and ret != "()"
    check_tags(problems, path, doc, Expected(split_params(params), type_params(m.group(5)),
                                             returns, bool(m.group(2)), body_panics(lines, i)))


def check_file(path: Path, problems: list) -> None:
    lines = path.read_text().splitlines()
    rel = path.relative_to(ROOT)
    first = next((line for line in lines if line.strip()), "")
    if not first.strip().startswith("/**") or "@module" not in "\n".join(lines[:3]):
        problems.append(f"{rel}:1: the file does not start with a /** @module */ comment")
    elif not str(rel).startswith("std/core/"):
        end = next((n for n, line in enumerate(lines) if "*/" in line), 0)
        if not any(line.strip().startswith("* @example") for line in lines[:end + 1]):
            problems.append(f"{rel}:1: the module comment has no @example")
    complexity = str(rel).startswith(COMPLEXITY)
    enum_depth = None   # brace depth inside a public enum, for its variants
    depth = 0
    for i, line in enumerate(lines):
        code = line.split("//")[0]
        stripped = code.strip()
        if stripped.startswith("*") or stripped.startswith("/*"):
            continue
        fn = FN_RE.match(code)
        ty = TYPE_RE.match(code)
        if fn:
            check_fn(problems, rel, lines, i, fn)
            doc = doc_before(lines, i)
            if complexity and fn.group(1) and doc and not any(
                    t[0] == "@complexity" for t in doc.tags):
                problems.append(f"{rel}:{i + 1}: `{fn.group(3)}` has no @complexity")
        elif ty:
            doc = doc_before(lines, i)
            if doc is None and ty.group(1):
                problems.append(f"{rel}:{i + 1}: public {ty.group(3)} `{ty.group(4)}` has no "
                                "/** */ documentation")
            elif doc is not None:
                check_tags(problems, rel, doc, Expected(type_params=type_params(ty.group(6))))
            if ty.group(3) == "enum" and ty.group(1):
                enum_depth = depth + 1
        elif CONST_RE.match(code) or FIELD_RE.match(code):
            if doc_before(lines, i) is None:
                problems.append(f"{rel}:{i + 1}: public declaration has no /** */ "
                                "documentation")
        elif enum_depth == depth and VARIANT_RE.match(code) and doc_before(lines, i) is None:
            problems.append(f"{rel}:{i + 1}: variant `{VARIANT_RE.match(code).group(1)}` has "
                            "no /** */ documentation")
        depth += code.count("{") - code.count("}")
        if enum_depth is not None and depth < enum_depth:
            enum_depth = None


def main() -> int:
    problems = []
    for source in SOURCES:
        for path in sorted((ROOT / source).rglob("*.of")):
            if not path.stem.endswith("_test"):
                check_file(path, problems)
    for p in problems:
        print(f"error: {p}")
    print(f"docs: {len(problems)} errors")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
