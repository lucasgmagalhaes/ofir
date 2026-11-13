#!/usr/bin/env python3
"""Checks the SHA-256 of a compiler seed (docs/bootstrap.md) before
`make check-seed` compiles it: a seed that `make seed` wrote must match the
`.sha256` file next to it (the sources may be newer than the release of the
same version), and a seed without one, as a published seed downloaded alone,
must match the checksum recorded for its name in seed/SEEDS.md. A seed with
neither record is refused, so no unaudited IR becomes a compiler.

    python3 tools/check_seed_sum.py <seed.ll> [--seeds seed/SEEDS.md]
"""

import argparse
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# A row of seed/SEEDS.md: | [version](url) | target | `file` | `sha256` |
ROW = re.compile(r"^\|[^|]*\|[^|]*\|\s*`([^`]+)`\s*\|\s*`([0-9a-f]{64})`\s*\|", re.MULTILINE)


def published(seeds: Path) -> dict:
    """The checksum of each seed listed in `seeds`, by file name."""
    return {name: digest for name, digest in ROW.findall(seeds.read_text())}


def recorded(seed: Path, seeds: Path) -> tuple:
    """The expected checksum of `seed` and where it is recorded, or None."""
    side = seed.with_name(seed.name + ".sha256")
    if side.exists():
        fields = side.read_text().split()
        if fields:
            return fields[0], str(side)
    listed = published(seeds).get(seed.name)
    if listed:
        return listed, str(seeds)
    return None, None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("seed", type=Path)
    parser.add_argument("--seeds", type=Path, default=ROOT / "seed" / "SEEDS.md")
    args = parser.parse_args()
    if not args.seed.is_file():
        print(f"check_seed_sum: {args.seed}: no such seed")
        return 1
    expected, where = recorded(args.seed, args.seeds)
    if expected is None:
        print(f"check_seed_sum: {args.seed.name}: no checksum in {args.seeds} "
              f"and no {args.seed.name}.sha256 next to it")
        return 1
    actual = hashlib.sha256(args.seed.read_bytes()).hexdigest()
    if actual != expected:
        print(f"check_seed_sum: {args.seed.name}: SHA-256 {actual}, "
              f"but {where} records {expected}")
        return 1
    print(f"check_seed_sum: {args.seed.name} matches {where}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
