#!/usr/bin/env python3
"""ZIP de fuentes y documentación, sin .git, compilados ni temporales."""
import argparse
import hashlib
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, default=ROOT / "komikan-concurrente.zip")
arguments = parser.parse_args()
excluded = {".git", "build", "work", "output", "__pycache__"}
files = sorted(p for p in ROOT.rglob("*") if p.is_file()
               and not any(part in excluded for part in p.relative_to(ROOT).parts)
               and p.name != "torneo"
               and p.suffix not in {".zip", ".o", ".d", ".so", ".log", ".tmp", ".pyc"})
required = {"Makefile", "README.md", ".gitignore", "src/main.c", "docs/informe.pdf",
            "tests/test_integration.py", "configs/ejemplo.txt"}
missing = required - {str(p.relative_to(ROOT)) for p in files}
if missing:
    raise SystemExit(f"Faltan entregables: {sorted(missing)}")
arguments.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(arguments.output, "w", zipfile.ZIP_DEFLATED) as archive:
    for path in files:
        archive.write(path, Path("komikan-concurrente") / path.relative_to(ROOT))
    manifest = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(ROOT)}\n" for p in files)
    archive.writestr("komikan-concurrente/MANIFEST.sha256", manifest)
with zipfile.ZipFile(arguments.output) as archive:
    if archive.testzip() is not None:
        raise SystemExit("ZIP corrupto")
print(f"{arguments.output}: {len(files)} archivos + MANIFEST.sha256; integridad ZIP OK")
