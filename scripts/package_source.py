#!/usr/bin/env python3
"""Create a clean GitHub-style source ZIP without build, runtime or dependency checkouts."""
from __future__ import annotations

import argparse
import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SKIP_PARTS = {".git", ".deps", "build", "build-logs", "dist", "runtime", "__pycache__"}
SKIP_FILES = {
    ".DS_Store",
    ".resolved-dependencies.json",
    ".resolved-dkr-source.json",
    "dkr.us.v77.generated.toml",
    "resolved-runtime-dependencies.json",
}


def include(path: pathlib.Path) -> bool:
    relative = path.relative_to(ROOT)
    if any(part in SKIP_PARTS for part in relative.parts):
        return False
    if relative.parts and relative.parts[0] == "extern" and len(relative.parts) > 2:
        return False
    if relative.parts[:2] == ("runtime-recomp", "RecompiledFuncs"):
        return False
    return path.name not in SKIP_FILES


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default=str(ROOT.parent / "DKRPort-NativeLauncher-Milestone0.4.4.zip"))
    args = parser.parse_args()
    output = pathlib.Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()

    archive_root = pathlib.Path("DKRPort")
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(ROOT.rglob("*")):
            if path.is_file() and include(path):
                relative = archive_root / path.relative_to(ROOT)
                info = zipfile.ZipInfo(relative.as_posix(), date_time=(2026, 7, 28, 12, 0, 0))
                mode = 0o755 if path.suffix in {".sh", ".py"} else 0o644
                info.external_attr = mode << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, path.read_bytes())
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
