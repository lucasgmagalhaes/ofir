#!/usr/bin/env python3
"""Checks the documentation site of `ofir doc` (OFIR-63, ADR 0075).

It documents a package of two modules and checks:

- `search.js` lists the declarations, and each page loads it and has the
  search box;
- a `code` span that names a declaration links to it, in its module or
  in another one, and code blocks and signatures are not linked;
- private declarations are left out, and `--private` documents them;
- the package's name and version head the pages;
- no page or script refers to the network (the site works offline);
- the examples of the comments pass as doc tests under `ofir test`.

    python3 tools/check_doc_site.py --compiler build/stage1/ofir
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

MANIFEST = '{\n  "name": "shapes",\n  "version": "1.2.0"\n}\n'

UNITS = """\
/**
 * @module
 * Units of length.
 */

/**
 * A length in meters. See `geometry.Point` and `Meter.double`.
 *
 * ```ofir
 * import units.Meter
 * import std.testing.assert
 *
 * assert(Meter{value: 2}.double().value == 4)
 * ```
 */
pub struct Meter {
    /** Its value. */
    pub value: int

    /** Twice the length. */
    pub fn double(self) Meter {
        return Meter{value: self.value * 2}
    }
}
"""

GEOMETRY = """\
/**
 * @module
 * Points, measured in `Meter`.
 */

import units.Meter

/**
 * A point; its distance from the origin is `norm`, and `missing` is no
 * declaration.
 *
 * ```ofir no_run
 * norm
 * ```
 */
pub struct Point {
    /** Its abscissa. */
    pub x: int
}

/** The distance of a point from the origin, as a `Meter`. */
pub fn norm(p: Point) Meter {
    return Meter{value: helper(p.x)}
}

/** Its absolute value: a private helper. */
fn helper(n: int) int {
    return if n < 0 { -n } else { n }
}
"""


def fail(message):
    print(f"doc-site: {message}", file=sys.stderr)
    sys.exit(1)


def run(cmd, cwd):
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=600)
    if result.returncode != 0:
        fail(f"{' '.join(cmd)} exited with {result.returncode}:\n"
             f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result


def expect(text, want, where):
    if want not in text:
        fail(f"{where} lacks {want!r}")


def refuse(text, unwanted, where):
    if unwanted in text:
        fail(f"{where} has {unwanted!r}")


def doc(compiler, cwd, *args, status):
    result = subprocess.run([compiler, "doc", *args], cwd=cwd, capture_output=True, text=True,
                            errors="replace", timeout=600)
    if result.returncode != status:
        fail(f"doc {' '.join(args)} exited with {result.returncode}, not {status}:\n"
             f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result


def failures(compiler, work):
    """The arguments, manifests and files `ofir doc` refuses or works around."""
    expect(doc(compiler, work, "-o", status=2).stderr, "`-o` needs a directory", "doc -o")
    expect(doc(compiler, work, "--wide", status=2).stderr, "unknown option `--wide`",
           "doc --wide")
    expect(doc(compiler, work, "missing", status=2).stderr, "cannot read `missing`",
           "doc missing")

    tree = work / "tree"
    (tree / "nested").mkdir(parents=True)
    (tree / ".hidden").mkdir()
    (tree / "good.of").write_text(UNITS)
    (tree / "nested" / "index.of").write_text(UNITS)
    (tree / "nested" / "inner.of").write_text(UNITS)
    (tree / "good_test.of").write_text("bad(")
    (tree / ".hidden" / "skipped.of").write_text("bad(")
    (tree / "notes.txt").write_text("bad(")
    printed = doc(compiler, work, "--stdout", "tree", status=0).stdout
    expect(printed, "<title>good", "doc --stdout")
    expect(printed, "<title>nested", "doc --stdout")
    expect(printed, "<title>nested.inner", "doc --stdout")
    refuse(printed, "skipped", "doc --stdout")
    expect(doc(compiler, work, "--stdout", "tree/good.of", status=0).stdout, "<title>good",
           "doc of a file")
    (tree / "unlexed.of").write_text('fn f() {\n    print("hi)\n}\n')
    (tree / "unparsed.of").write_text("fn f( {\n}\n")
    expect(doc(compiler, work, "--stdout", "tree", status=1).stderr, "error",
           "doc of files that do not lex or parse")
    if Path("/proc/self/mem").exists():   # Linux: reading it fails at the first byte
        alone = work / "alone"
        alone.mkdir()
        (alone / "memory.of").symlink_to("/proc/self/mem")
        expect(doc(compiler, work, "--stdout", "alone", status=1).stderr, "cannot read",
               "doc of a file that cannot be read")

    site = work / "site"
    site.mkdir()
    (site / "units.of").write_text(UNITS)
    (site / "ofir.json").write_text("{ not json")
    doc(compiler, site, "--stdout", status=0)
    (site / "ofir.json").write_text('{"name": "units"}')
    expect(doc(compiler, site, "--stdout", status=0).stdout, "units", "a manifest without version")
    (site / "file").write_text("")
    expect(doc(compiler, site, "-o", "file/out", status=2).stderr, "cannot create",
           "doc -o under a file")
    for blocked in ["units.html", "index.html", "search.js"]:
        out = site / f"out-{blocked}"
        (out / blocked).mkdir(parents=True)
        expect(doc(compiler, site, "-o", out.name, status=2).stderr, "cannot write",
               f"doc with {blocked} a directory")


def check(compiler, work):
    (work / "ofir.json").write_text(MANIFEST)
    (work / "src").mkdir()
    (work / "src" / "units.of").write_text(UNITS)
    (work / "src" / "geometry.of").write_text(GEOMETRY)

    run([compiler, "doc", "-o", "out"], work)
    out = work / "out"
    search = (out / "search.js").read_text()
    for entry in ['["Meter", "units"', '["Meter.double", "units"', '["norm", "geometry"',
                  '["Point.x", "geometry"']:
        expect(search, entry, "search.js")
    refuse(search, '"helper"', "search.js")
    expect(search, "getElementById(\"search\")", "search.js")
    expect(search, "No matching declarations.", "search.js")
    expect(search, "link.textContent =", "search.js")
    refuse(search, "item.innerHTML", "search.js")

    pages = {name: (out / f"{name}.html").read_text() for name in ["index", "units", "geometry"]}
    for name, page in pages.items():
        expect(page, '<script src="search.js" defer></script>', f"{name}.html")
        expect(page, 'id="search"', f"{name}.html")
        expect(page, "shapes 1.2.0", f"{name}.html")
        expect(page, 'name="viewport"', f"{name}.html")
        expect(page, '<main id="content">', f"{name}.html")
        expect(page, 'href="#content">Skip to content</a>', f"{name}.html")
        expect(page, 'for="search">Search documentation</label>', f"{name}.html")
    for name, text in [*pages.items(), ("search.js", search)]:
        if re.search(r"(src|href)=\"(https?:)?//", text) or "@import" in text:
            fail(f"{name} refers to the network")

    units, geometry = pages["units"], pages["geometry"]
    expect(units, '<a href="geometry.html#Point"><code>geometry.Point</code></a>', "units.html")
    expect(units, '<a href="units.html#Meter.double"><code>Meter.double</code></a>',
           "units.html")
    expect(geometry, '<a href="units.html#Meter"><code>Meter</code></a>', "geometry.html")
    expect(geometry, '<a href="geometry.html#norm"><code>norm</code></a>', "geometry.html")
    expect(geometry, "<code>missing</code>", "geometry.html")
    refuse(geometry, '<a href="geometry.html#norm"><code>norm\n', "geometry.html")
    refuse(geometry, "<pre class=\"signature\"><a", "geometry.html")
    refuse(geometry, "helper", "geometry.html")

    run([compiler, "doc", "--private", "-o", "private"], work)
    private = (work / "private" / "geometry.html").read_text()
    expect(private, 'id="helper"', "geometry.html with --private")
    expect((work / "private" / "search.js").read_text(), '["helper", "geometry"',
           "search.js with --private")

    # A doc test imports its module by the file's stem, as a file outside a
    # package does.
    with tempfile.TemporaryDirectory() as alone:
        (Path(alone) / "units.of").write_text(UNITS)
        tested = run([compiler, "test", "units.of"], Path(alone))
    expect(tested.stdout, "(doc test) ... ok", "ofir test")
    print("doc-site: ok")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="build/stage1/ofir")
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        check(compiler, Path(tmp))
    with tempfile.TemporaryDirectory() as tmp:
        failures(compiler, Path(tmp))
    return 0


if __name__ == "__main__":
    sys.exit(main())
