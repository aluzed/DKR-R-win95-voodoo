#!/usr/bin/env python3
"""Create and validate a deterministic source ZIP from the committed revision."""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "dist" / "DKRPort-1.0.0-rc3-Source.zip"
DENIED_SUFFIXES = {".z64", ".v64", ".n64", ".o2r", ".otr", ".eep", ".mpk"}
N64_HEADERS = {b"\x80\x37\x12\x40", b"\x37\x80\x40\x12", b"\x40\x12\x37\x80"}


def validate_archive(path: pathlib.Path) -> None:
    inspected = 0
    with zipfile.ZipFile(path, "r") as archive:
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            inspected += 1
            member = pathlib.PurePosixPath(entry.filename)
            if member.is_absolute() or ".." in member.parts:
                raise RuntimeError(f"Unsafe source archive path: {entry.filename}")
            if member.suffix.lower() in DENIED_SUFFIXES:
                raise RuntimeError(f"Prohibited game-data entry: {entry.filename}")
            with archive.open(entry, "r") as stream:
                if stream.read(4) in N64_HEADERS:
                    raise RuntimeError(f"N64 ROM header in source archive: {entry.filename}")
    if inspected == 0:
        raise RuntimeError("Source archive is empty")
    print(f"Source archive scan passed: {inspected} files inspected")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default=str(DEFAULT_OUTPUT))
    args = parser.parse_args()
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        print(f"Refusing to overwrite existing source archive: {output}", file=sys.stderr)
        return 2
    output.parent.mkdir(parents=True, exist_ok=True)

    revision = subprocess.run(
        ["git", "rev-parse", "--verify", "HEAD"], cwd=ROOT,
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    subprocess.run(
        [
            "git", "archive", "--format=zip", "--prefix=DKRPort/",
            f"--output={output}", revision,
        ],
        cwd=ROOT,
        check=True,
    )
    validate_archive(output)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
