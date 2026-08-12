#!/usr/bin/env python3
"""E09-S01 — Rend `voodoo2.inf` compatible avec la Voodoo 2 émulée par 86Box.

Le pilote de référence 3dfx ne se lie qu'à `PCI\\VEN_121A&DEV_0002`, l'identifiant
des vraies cartes Voodoo 2. 86Box, lui, expose sa Voodoo 2 avec `DEV_0001` —
l'identifiant de la Voodoo Graphics de première génération. Le POST le montre
dans sa colonne « Device ID ». Conséquence : l'auto-détection de Windows ne
reconnaît jamais la carte, quel que soit le chemin indiqué à l'assistant.

Ce script ajoute la liaison `DEV_0001` **à côté** de celle d'origine, aux trois
endroits où l'INF la déclare. Les vraies cartes restent donc prises en charge.

    scripts/patch_voodoo2_inf.py --inf voodoo2.inf --output voodoo2.patched.inf

À retenir au-delà de l'installation : sur cette plate-forme de test,
l'identifiant PCI ment sur le modèle de carte. La détection à l'exécution du
backend Glide (E05-S01) doit s'appuyer sur `grSstQueryBoards` / `grGet`, pas sur
le bus PCI.
"""
from __future__ import annotations

import argparse
import pathlib
import sys

REAL = "DEV_0002"          # vraies cartes Voodoo 2
EMULATED = "DEV_0001"      # ce que 86Box présente

# Les trois formes sous lesquelles l'INF déclare la liaison.
PREFIXES = (
    "%PCI\\VEN_121A&DEV_0002.DeviceDesc%=",
    "PCI\\VEN_121A&DEV_0002.DeviceDesc=",
)
EXACT = "HKLM,Enum\\PCI\\VEN_121A&DEV_0002"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inf", required=True, type=pathlib.Path,
                    help="voodoo2.inf extrait du paquet de pilotes 3dfx")
    ap.add_argument("--output", required=True, type=pathlib.Path)
    args = ap.parse_args()

    if not args.inf.is_file():
        print(f"error: INF introuvable : {args.inf}", file=sys.stderr)
        return 1

    # Les INF de cette époque sont en page de code Windows, pas en UTF-8.
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
        print(f"error: aucune liaison {REAL} trouvée — est-ce bien voodoo2.inf ?",
              file=sys.stderr)
        return 1

    args.output.write_bytes("".join(out).encode("cp1252"))
    print(f"{added} liaisons {EMULATED} ajoutées → {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
