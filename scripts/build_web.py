"""Rebuild deterministic compressed assets before PlatformIO builds/uploads."""
from pathlib import Path
import gzip
try:
    Import("env")
    root = Path(env.subst("$PROJECT_DIR"))
except NameError:
    root = Path(__file__).resolve().parent.parent
for name in ("index.html", "app.js", "style.css"):
    data = gzip.compress((root / "web" / name).read_bytes(), compresslevel=9, mtime=0)
    output = root / "data" / (name + ".gz")
    if not output.exists() or output.read_bytes() != data:
        output.write_bytes(data)
