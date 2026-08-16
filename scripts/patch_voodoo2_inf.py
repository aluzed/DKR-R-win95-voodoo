#!/usr/bin/env python3
"""E09-S01 - makes `voodoo2.inf` compatible with the Voodoo 2 86Box emulates.

3dfx's reference driver only binds to `PCI\\VEN_121A&DEV_0002`, the identifier of
real Voodoo 2 boards. 86Box, for its part, exposes its Voodoo 2 with `DEV_0001` -
the identifier of the first-generation Voodoo Graphics. The POST shows it in its
"Device ID" column. Consequence: Windows's auto-detection never recognises the
board, whatever path is given to the wizard.

This script adds the `DEV_0001` binding **alongside** the original one, at the
three places where the INF declares it. Real boards therefore stay supported.

    scripts/patch_voodoo2_inf.py --inf voodoo2.inf --output voodoo2.patched.inf

Worth remembering beyond the installation: on this test platform, the PCI
identifier lies about the board model. The Glide backend's run-time detection
(E05-S01) must rely on `grSstQueryBoards` / `grGet`, not on the PCI bus.
"""
from __future__ import annotations

import argparse
import pathlib
import sys

REAL = "DEV_0002"          # real Voodoo 2 boards
EMULATED = "DEV_0001"      # what 86Box presents

# The three forms in which the INF declares the binding.
PREFIXES = (
    "%PCI\\VEN_121A&DEV_0002.DeviceDesc%=",
    "PCI\\VEN_121A&DEV_0002.DeviceDesc=",
)
EXACT = "HKLM,Enum\\PCI\\VEN_121A&DEV_0002"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inf", required=True, type=pathlib.Path,
                    help="voodoo2.inf extracted from the 3dfx driver package")
    ap.add_argument("--output", required=True, type=pathlib.Path)
    args = ap.parse_args()

    if not args.inf.is_file():
        print(f"error: INF not found: {args.inf}", file=sys.stderr)
        return 1

    # INF files of this era are in a Windows code page, not UTF-8.
    text = args.inf.read_bytes().decode("cp1252")
    out: list[str] = []
    added = 0
    for line in text.splitlines(True):
        out.append(line)
        stripped = line.strip()
        if stripped.startswith(PREFIXES) or stripped == EXACT:
            out.append(line.replace(REAL, EMULATED))
            added += 1

    if added == 0:
        print(f"error: no {REAL} binding found - is this really voodoo2.inf?",
              file=sys.stderr)
        return 1

    args.output.write_bytes("".join(out).encode("cp1252"))
    print(f"{added} {EMULATED} bindings added -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
