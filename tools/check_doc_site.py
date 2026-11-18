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

    pages = {name: (out / f"{name}.html").read_text() for name in ["index", "units", "geometry"]}
    for name, page in pages.items():
        expect(page, '<script src="search.js" defer></script>', f"{name}.html")
        expect(page, 'id="search"', f"{name}.html")
        expect(page, "shapes 1.2.0", f"{name}.html")
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
