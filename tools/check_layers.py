#!/usr/bin/env python3
"""Checks the layers of the stage 1 compiler (ADR 0093).

Each top-level folder of `compiler/` may import only the folders listed
for it in LAYERS, besides itself and `std`: the passes import downwards
(base, diag, syntax, sema, air, codegen) and the tools on top import the
passes, never the other way. A folder that is not in LAYERS is an error,
so a new one states its place here.

    python3 tools/check_layers.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT / "compiler"

PASSES = ["base", "diag", "syntax", "sema", "air", "codegen"]

# The folders each folder may import; `main.of` may import any.
LAYERS = {name: set(PASSES[:i]) for i, name in enumerate(PASSES)}
LAYERS.update({
    "driver": {"base", "diag", "syntax", "sema"},
    "doc": {"base", "diag", "syntax"},
    "fmt": {"base", "diag", "syntax"},
    "lsp": {"base", "diag", "syntax", "sema", "driver"},
})

IMPORT = re.compile(r"^(?:pub )?import ([A-Za-z_][A-Za-z0-9_]*)", re.M)


def layer(path: Path) -> str:
    parts = path.relative_to(COMPILER).parts
    return parts[0] if len(parts) > 1 else ""


def main() -> int:
    errors = []
    for path in sorted(COMPILER.rglob("*.of")):
        name = layer(path)
        if not name:
            continue   # main.of is the top
        rel = path.relative_to(ROOT)
        if name not in LAYERS:
            errors.append(f"{rel}: folder `{name}` has no place in LAYERS")
            continue
        text = path.read_text()
        for match in IMPORT.finditer(text):
            target = match.group(1)
            if target in (name, "std") or target in LAYERS[name]:
                continue
            line = text.count("\n", 0, match.start()) + 1
            errors.append(f"{rel}:{line}: `{name}` may not import `{target}`")
    for error in errors:
        print(error, file=sys.stderr)
    print(f"layers: {len(LAYERS)} folders, {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
