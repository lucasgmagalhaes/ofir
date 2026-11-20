#!/usr/bin/env python3
"""Checks the layers of the stage 1 compiler (ADR 0093).

Each top-level folder of `compiler/` may import only the folders listed
for it in LAYERS, besides itself and `std`: the passes import downwards
(base, diag, syntax, sema, air, codegen) and the tools on top import the
passes, never the other way. A folder that is not in LAYERS is an error,
so a new one states its place here. INNER orders the subfolders of a
folder the same way: `driver/pkg` must not import `driver/build`.

    python3 tools/check_layers.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT / "compiler"

PASSES = ["base", "diag", "syntax", "sema", "air", "codegen"]

# The folders each folder may import; `cli` and `main.of` may import any.
LAYERS = {name: set(PASSES[:i]) for i, name in enumerate(PASSES)}
LAYERS.update({
    "driver": {"base", "diag", "syntax", "sema"},
    "doc": {"base", "diag", "syntax"},
    "fmt": {"base", "diag", "syntax"},
    "lsp": {"base", "diag", "syntax", "sema", "driver"},
    "cli": set(PASSES) | {"driver", "doc", "fmt", "lsp"},
})

# Inside a folder: the modules of its own folder that each subfolder may
# import, by prefix (a subfolder imports only those below it).
INNER = {
    "driver/pkg": ["driver.pkg.", "driver.version"],
    "driver/build": ["driver.pkg.", "driver.build.", "driver.version"],
    "syntax/lex": ["syntax.lex."],
    "syntax/ast": ["syntax.lex.", "syntax.ast."],
    "sema/core": ["sema.core."],
    "sema/resolve": ["sema.core.", "sema.resolve."],
    "air/ir": ["air.ir."],
    "air/opt": ["air.ir.", "air.opt."],
    "air/lower": ["air.ir.", "air.lower."],
}

IMPORT = re.compile(r"^(?:pub )?import ([A-Za-z_][A-Za-z0-9_.]*)", re.M)


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
        inner = next((v for k, v in INNER.items()
                      if path.relative_to(COMPILER).as_posix().startswith(k + "/")), None)
        text = path.read_text()
        for match in IMPORT.finditer(text):
            module = match.group(1)
            target = module.split(".")[0]
            line = text.count("\n", 0, match.start()) + 1
            if target == name and inner is not None and not module.startswith(tuple(inner)):
                where = rel.parent.relative_to("compiler").as_posix()
                errors.append(f"{rel}:{line}: `{where}` may not import `{module}`")
            if target in (name, "std") or target in LAYERS[name]:
                continue
            errors.append(f"{rel}:{line}: `{name}` may not import `{target}`")
    for error in errors:
        print(error, file=sys.stderr)
    print(f"layers: {len(LAYERS)} folders, {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
