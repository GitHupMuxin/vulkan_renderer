# scripts/compile_shaders.py 

import subprocess
import sys
from pathlib import Path

SHADER_DIR = Path("data/shaders")
INCLUDE_DIR = SHADER_DIR / "includes"
EXTENSIONS = (".vert", ".frag", ".comp", ".geom", ".tesc", ".tese")

sources = [f for f in SHADER_DIR.iterdir()
           if f.suffix.lower() in EXTENSIONS]

compiled = 0;
skipped = 0;

for src in sources:
    spv = src.with_suffix(src.suffix + ".spv")
    if spv.exists() and spv.stat().st_mtime >= src.stat().st_mtime:
        skipped += 1
        continue

    print(f"Compiling {src.name}...")
    subprocess.run(
        ["glslc", "-I", str(INCLUDE_DIR), "-o", str(spv), str(src)],
        check=True
    )
    compiled += 1


print(f"Done. {compiled} compiled, {skipped} skipped.")
