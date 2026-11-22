#!/usr/bin/env python3
"""Checks `ofir lsp` (docs/design/toolchain.md) where the `tests/lsp`
sessions cannot reach:

- the framing: input that cannot be read or ends without `exit`, a header or a body that is
  not UTF-8, a header without `Content-Length` or with a bad one, and a body
  shorter than its length;
- files on disk: a document that does not lex, parse or load its imports,
  an error in an imported file, URIs of files that are not open (relative,
  with a Windows drive, with bytes to escape), uses found from two open
  documents, hover on a parameter and on a documented declaration with an
  attribute, positions after non-ASCII text, and names a rename refuses.

    python3 tools/check_lsp_wire.py --compiler build/stage1/ofir
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def fail(message: str) -> None:
    print(f"lsp-wire: {message}", file=sys.stderr)
    sys.exit(1)


def frame(body: bytes) -> bytes:
    return b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body


def message(**fields) -> bytes:
    return frame(json.dumps({"jsonrpc": "2.0", **fields}).encode())


CLOSE = message(id=99, method="shutdown") + message(method="exit")


def serve(compiler: str, data: bytes, cwd: Path, status: int = 0) -> list:
    result = subprocess.run([compiler, "lsp"], input=data, cwd=cwd, capture_output=True,
                            timeout=300)
    if result.returncode != status:
        fail(f"lsp exited with {result.returncode}, not {status}, on {data[:300]!r}:\n"
             f"{result.stderr.decode(errors='replace')[-2000:]}")
    out, rest = [], result.stdout
    while rest:
        header, _, rest = rest.partition(b"\r\n\r\n")
        length = int(header.split(b":")[1])
        out.append(json.loads(rest[:length]))
        rest = rest[length:]
    return out


def answer(messages: list, ident: int):
    for m in messages:
        if m.get("id") == ident:
            return m
    fail(f"no answer to request {ident}: {messages}")


def published(messages: list, uri: str) -> list:
    found = [m for m in messages if m.get("method") == "textDocument/publishDiagnostics" and
             m["params"]["uri"] == uri]
    if not found:
        fail(f"no diagnostics published for {uri}: {messages}")
    return found[-1]["params"]["diagnostics"]


def framing(compiler: str, work: Path) -> None:
    folder = os.open(work, os.O_RDONLY)   # reading a directory fails
    try:
        if subprocess.run([compiler, "lsp"], stdin=folder, capture_output=True,
                          timeout=300).returncode != 1:
            fail("an input that cannot be read does not give 1")
    finally:
        os.close(folder)
    serve(compiler, b"", work, status=1)   # the input ends without `exit`
    serve(compiler, b"Content-Length: 2\xff\r\n\r\n{}", work, status=1)
    serve(compiler, b"Content-Length: 4\r\n\r\n\"\xff\xfe\"", work, status=1)
    serve(compiler, b"Content-Length: 50\r\n\r\n{}", work, status=1)
    for header in [b"X-Other: 1\r\n\r\n", b"Content-Length: 1x\r\n\r\n"]:
        got = serve(compiler, header + CLOSE, work)
        if got[0].get("error", {}).get("code") != -32700:
            fail(f"a header {header!r} is not answered as a parse error: {got}")


def opened(uri: str, text: str) -> bytes:
    return message(method="textDocument/didOpen",
                   params={"textDocument": {"uri": uri, "languageId": "ofir", "version": 1,
                                            "text": text}})


def at(ident: int, method: str, uri: str, line: int, character: int, **params) -> bytes:
    return message(id=ident, method=method,
                   params={"textDocument": {"uri": uri},
                           "position": {"line": line, "character": character}, **params})


UTIL = ("pub fn twice(x: int) int {\n    return x * 2\n}\n\n"
        "pub fn quad(x: int) int {\n    return twice(twice(x))\n}\n")
MAIN = ("import util\n\nfn main() {\n    const n = \"é\".len() + util.twice(1)\n"
        "    print(\"{n}\")\n}\n")
DOCUMENTED = ("struct P {\n    x: int\n}\n\n/** Doubles a point. */\n@inline\n"
              "fn double(p: P) int {\n    return p.x * 2\n}\n\n"
              "fn main() {\n    const v = double(P{x: 1})\n    print(\"{v}\")\n}\n")

SHAPE = ("enum Shape {\n    Circle(int)\n    Dot\n}\n\nfn area(s: Shape) int {\n"
         "    return match s {\n        Shape.Circle( r ) => r\n        Shape.Dot => 0\n"
         "    }\n}\n\n"
         "fn main() {\n    const a = area(Shape.Circle(2))\n    print(\"{a}\")\n}\n")


def documents(compiler: str, work: Path) -> None:
    errors = {"unlexed": "fn main() {\n    print(\"hi)\n}\n", "unparsed": "fn main( {\n}\n",
              "unloaded": "import nowhere\n\nfn main() {\n}\n",
              "broken": "import bad\n\nfn main() {\n}\n"}
    (work / "bad.of").write_text("fn f( {\n}\n")
    for name, text in errors.items():
        uri = (work / f"{name}.of").as_uri()
        got = serve(compiler, opened(uri, text) + CLOSE, work)
        if not published(got, uri):
            fail(f"{name}.of has no diagnostics")
    uri = (work / "broken.of").as_uri()
    got = serve(compiler, opened(uri, errors["broken"]) + CLOSE, work)
    if not any("bad.of:" in d["message"] for d in published(got, uri)):
        fail(f"an error in an imported file is not named: {got}")

    for dir_name, uri_dir in [("sp ace", "sp%20ace"), ("C:", "file:///C:")]:
        folder = work / dir_name
        folder.mkdir()
        (folder / "util.of").write_text(UTIL)
        (folder / "main.of").write_text(MAIN)
        uri = f"{uri_dir}/main.of"
        got = serve(compiler, opened(uri, MAIN) + at(2, "textDocument/definition", uri, 3, 32)
                    + CLOSE, work)
        target = answer(got, 2)["result"]["uri"]
        want = ((work / "sp ace" / "util.of").as_uri() if dir_name == "sp ace"
                else "file:///C:/util.of")
        if target != want:
            fail(f"the definition in {dir_name!r} is at {target}, not {want}")

    folder = work / "pair"
    folder.mkdir()
    (folder / "util.of").write_text(UTIL)
    (folder / "main.of").write_text(MAIN)
    (folder / "other.of").write_text(MAIN)
    main_uri, util_uri = (folder / "main.of").as_uri(), (folder / "util.of").as_uri()
    other_uri = (folder / "other.of").as_uri()
    got = serve(compiler, opened(main_uri, MAIN) + opened(other_uri, MAIN)
                + at(2, "textDocument/references", main_uri, 3, 32,
                     context={"includeDeclaration": True})
                + at(3, "textDocument/rename", util_uri, 0, 8, newName="1x")
                + at(4, "textDocument/rename", util_uri, 0, 8, newName="a-b")
                + at(5, "textDocument/hover", main_uri, 3, 32) + CLOSE, work)
    places = [(p["uri"], p["range"]["start"]["line"]) for p in answer(got, 2)["result"]]
    if places != sorted(set(places)) or len(places) != 3:
        fail(f"the references are {places}")
    for ident in (3, 4):
        if answer(got, ident).get("error", {}).get("code") != -32602:
            fail(f"a bad new name is not refused: {answer(got, ident)}")
    hovered = answer(got, 5)["result"]
    if hovered["range"]["start"]["character"] != 31:
        fail(f"a position after non-ASCII text is misread: {hovered}")

    uri = (work / "documented.of").as_uri()
    got = serve(compiler, opened(uri, DOCUMENTED) + at(2, "textDocument/hover", uri, 7, 11)
                + at(3, "textDocument/hover", uri, 11, 15) + CLOSE, work)
    parameter = answer(got, 2)["result"]["contents"]["value"]
    if "\np: P\n" not in parameter:
        fail(f"hover on a parameter shows {parameter!r}")
    documented = answer(got, 3)["result"]["contents"]["value"]
    if "Doubles a point." not in documented or "fn double" not in documented:
        fail(f"hover on a documented declaration shows {documented!r}")

    uri = (work / "shape.of").as_uri()
    got = serve(compiler, opened(uri, SHAPE)
                + at(2, "textDocument/references", uri, 7, 21, context={"includeDeclaration": True})
                + at(3, "textDocument/references", uri, 7, 15, context={"includeDeclaration": True})
                + CLOSE, work)
    if answer(got, 2)["result"] != []:
        fail(f"a space between names has references: {answer(got, 2)}")
    if len(answer(got, 3)["result"]) != 1:
        fail(f"a variant in a pattern is listed twice: {answer(got, 3)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp).resolve()   # the server answers with real paths (macOS: /private/var)
        framing(compiler, work)
        documents(compiler, work)
    print("lsp-wire: ok")


if __name__ == "__main__":
    main()
