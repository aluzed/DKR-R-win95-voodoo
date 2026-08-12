#!/usr/bin/env python3
"""E01-S02 — refuse les en-tetes standard interdits sur la cible Windows 95.

    tools/win95/check-cpp-subset.py platform/win95 runtime-recomp/src
    tools/win95/check-cpp-subset.py --self-test

Il n'y a **pas de norme C++ a restreindre** : GCC 13 implemente tout C++20 pour
`i686-w64-mingw32`. Ce qui est interdit, ce sont des facilites de bibliotheque
qui font apparaitre dans la table d'imports des symboles que Windows 95
n'exporte pas — et sous Windows 95, un import manquant empeche le processus de
demarrer, meme si la fonction n'est jamais appelee.

Le compte de symboles absents attache a chaque en-tete est mesure, non presume :
voir `tools/win95/probes/build-probes.sh` et `docs/CPP-SUBSET.md`.

Sans verification automatique, une inclusion interdite se reintroduit a la
premiere contribution et ne se decouvre qu'au lancement sur la machine cible.
"""
import argparse
import pathlib
import re
import sys
import tempfile

# en-tete -> (nombre de symboles absents, remplacement)
FORBIDDEN = {
    "thread":             (6,  "CreateThread, via la couche de E02-S01"),
    "mutex":              (6,  "CRITICAL_SECTION, via la couche de E02-S01"),
    "shared_mutex":       (6,  "CRITICAL_SECTION, via la couche de E02-S01"),
    "condition_variable": (6,  "evenements Win32, via la couche de E02-S01"),
    "future":             (6,  "la couche de E02-S01"),
    "latch":              (6,  "la couche de E02-S01"),
    "barrier":            (6,  "la couche de E02-S01"),
    "semaphore":          (6,  "la couche de E02-S01"),
    "stop_token":         (6,  "la couche de E02-S01"),
    "filesystem":         (13, "couche fichiers en ...A (E02-S05)"),
    "syncstream":         (6,  "sans objet sur cette cible"),
}

SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".cc", ".cxx")
RX_INCLUDE = re.compile(r'^\s*#\s*include\s*<([A-Za-z0-9_./]+)>')

RED, GREEN, YELLOW, BLUE, OFF = (
    "\033[1;31m", "\033[1;32m", "\033[1;33m", "\033[1;34m", "\033[0m")


def scan(paths, quiet=False):
    """Renvoie la liste des (fichier, ligne, en-tete) fautifs."""
    bad = []
    files = 0
    for p in paths:
        root = pathlib.Path(p)
        candidates = ([root] if root.is_file()
                      else [f for f in root.rglob("*") if f.suffix in SOURCE_SUFFIXES])
        for f in candidates:
            if f.suffix not in SOURCE_SUFFIXES:
                continue
            files += 1
            try:
                text = f.read_text("latin-1", errors="ignore")
            except OSError:
                continue
            for i, line in enumerate(text.splitlines(), 1):
                m = RX_INCLUDE.match(line)
                if m and m.group(1) in FORBIDDEN:
                    bad.append((f, i, m.group(1)))
    if not quiet:
        print(f"{BLUE}==>{OFF} {files} fichier(s) examine(s)")
    return bad


def report(bad):
    for f, line, header in bad:
        n, replacement = FORBIDDEN[header]
        print(f"  {RED}INTERDIT{OFF}  {f}:{line}")
        print(f"            <{header}> — {n} symboles absents de Windows 95")
        print(f"            remplacement : {replacement}")


def self_test():
    """Un verificateur casse et un verificateur satisfait se taisent de la meme
    maniere : on lui soumet une inclusion interdite et une permise."""
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "propre.cpp").write_text(
            "#include <vector>\n#include <span>\n#include <format>\n"
            "#include <atomic>\n#include <chrono>\nint main(){return 0;}\n")
        (tmp / "sale.cpp").write_text(
            "#include <vector>\n#include <thread>\nint main(){return 0;}\n")

        print(f"{BLUE}==>{OFF} temoin propre : span, format, atomic, chrono")
        bad = scan([tmp / "propre.cpp"], quiet=True)
        if bad:
            report(bad)
            print(f"{RED}le temoin propre est refuse — le verificateur est trop strict{OFF}")
            return 1
        print(f"  {GREEN}accepte{OFF}")

        print(f"{BLUE}==>{OFF} temoin sale : <thread>")
        bad = scan([tmp / "sale.cpp"], quiet=True)
        if not bad:
            print(f"{RED}le temoin sale est accepte — le verificateur ne detecte rien{OFF}")
            return 1
        report(bad)
        print(f"  {GREEN}correctement refuse{OFF}")
    print(f"{BLUE}==>{OFF} {GREEN}le verificateur fonctionne{OFF}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", help="fichiers ou repertoires a examiner")
    ap.add_argument("--self-test", action="store_true",
                    help="eprouver le verificateur par injection")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.paths:
        ap.print_help()
        return 2

    bad = scan(args.paths)
    if bad:
        report(bad)
        print(f"  {RED}{len(bad)} inclusion(s) interdite(s){OFF} — voir docs/CPP-SUBSET.md")
        return 1
    print(f"  {GREEN}aucune inclusion interdite{OFF}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
