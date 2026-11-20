#!/usr/bin/env python3
"""Builds the release archive for the host (ADR 0082).

`ofir-<version>-<target>.tar.gz` holds `ofir-<version>/` in the
installation layout (docs/design/toolchain.md, section 5): `bin/ofir`,
`lib/ofir/std`, `lib/ofir/libofir_rt.a`, `share/ofir/errors`,
`share/ofir/LICENSE` and `install.sh`. The entries are sorted, owned by
0:0 and dated `SOURCE_DATE_EPOCH` (the last commit's time when unset), so
that the same commit gives the same bytes on the same host. The SHA-256
goes next to it, in the format of `sha256sum`.

    python3 tools/dist.py --compiler build/stage1/ofir --runtime build/libofir_rt.a
"""

import argparse
import gzip
import hashlib
import io
import os
import platform
import re
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def version() -> str:
    text = (ROOT / "compiler/driver/version.of").read_text()
    match = re.search(r'pub const VERSION: str = "([^"]+)"', text)
    if not match:
        sys.exit("dist: no VERSION in compiler/driver/version.of")
    return match.group(1)


def host_triple() -> str:
    """The triple of ADR 0065 for this host, as `driver.build.target.hostTriple`."""
    arch = platform.machine().lower()
    arch = {"amd64": "x86_64", "arm64": "aarch64"}.get(arch, arch)
    system = platform.system()
    if system == "Windows" or system.startswith(("MINGW", "MSYS", "CYGWIN")):
        return f"{arch}-w64-windows-gnu"
    if system == "Darwin":
        return f"{arch}-apple-darwin"
    return f"{arch}-linux-gnu"


def source_date() -> int:
    if "SOURCE_DATE_EPOCH" in os.environ:
        return int(os.environ["SOURCE_DATE_EPOCH"])
    out = subprocess.run(["git", "log", "-1", "--format=%ct"], cwd=ROOT,
                         capture_output=True, text=True)
    return int(out.stdout.strip()) if out.returncode == 0 and out.stdout.strip() else 0


def tree(compiler: Path, runtime: Path) -> list[tuple[str, Path]]:
    """The files of the archive: (path in the tree, source), sorted."""
    exe = "bin/ofir.exe" if compiler.suffix == ".exe" else "bin/ofir"
    files = [(exe, compiler), ("lib/ofir/libofir_rt.a", runtime),
             ("share/ofir/LICENSE", ROOT / "LICENSE"),
             ("install.sh", ROOT / "tools/install.sh")]
    for path in sorted((ROOT / "std").rglob("*")):
        if path.is_file() and not path.name.endswith("_test.of"):
            files.append((f"lib/ofir/std/{path.relative_to(ROOT / 'std').as_posix()}", path))
    for path in sorted((ROOT / "docs/errors").glob("E*.md")):
        files.append((f"share/ofir/errors/{path.name}", path))
    return sorted(files)


def archive(files: list[tuple[str, Path]], top: str, mtime: int) -> bytes:
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.PAX_FORMAT) as tar:
        dirs = set()
        for name, _ in files:
            parts = name.split("/")[:-1]
            for i in range(len(parts)):
                dirs.add("/".join(parts[:i + 1]))
        entries = [(d, None) for d in dirs] + files
        for name, source in sorted([(top, None)] + [(f"{top}/{n}", s) for n, s in entries],
                                   key=lambda e: e[0]):
            info = tarfile.TarInfo(name)
            info.mtime = mtime
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            if source is None:
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                tar.addfile(info)
                continue
            data = source.read_bytes()
            info.size = len(data)
            executable = name.endswith(("/bin/ofir", "/bin/ofir.exe", "/install.sh"))
            info.mode = 0o755 if executable else 0o644
            tar.addfile(info, io.BytesIO(data))
    out = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=out, mtime=0, compresslevel=9) as gz:
        gz.write(raw.getvalue())
    return out.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/dist")
    parser.add_argument("--version", default=None, help="the version (default: VERSION)")
    parser.add_argument("--target", default=None, help="the triple (default: the host's)")
    args = parser.parse_args()
    ver = args.version or version()
    target = args.target or host_triple()
    name = f"ofir-{ver}-{target}.tar.gz"
    data = archive(tree(args.compiler, args.runtime), f"ofir-{ver}", source_date())
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / name).write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    (args.out / f"{name}.sha256").write_text(f"{digest}  {name}\n")
    print(f"{digest}  {args.out / name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
