# scripts/compile_shaders.py 

import subprocess
import sys
from pathlib import Path

SHADER_DIR = Path("data/shaders")
INCLUDE_DIR = SHADER_DIR / "includes"
EXTENSIONS = (".vert", ".frag", ".comp", ".geom", ".tesc", ".tese")

sources = [f for f in SHADER_DIR.iterdir()
           if f.suffix.lower() in EXTENSIONS]
includes = [f for f in INCLUDE_DIR.rglob("*") if f.is_file()]
latest_include_mtime = max((f.stat().st_mtime for f in includes), default=0)

compiled = 0;
skipped = 0;

for src in sources:
    spv = src.with_suffix(src.suffix + ".spv")
    source_mtime = max(src.stat().st_mtime, latest_include_mtime)
    if spv.exists() and spv.stat().st_mtime >= source_mtime:
        skipped += 1
        continue

    print(f"Compiling {src.name}...")
    subprocess.run(
        ["glslc", "-I", str(INCLUDE_DIR), "-o", str(spv), str(src)],
        check=True
    )
    compiled += 1


print(f"Done. {compiled} compiled, {skipped} skipped.")
