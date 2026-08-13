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

# Derogation ligne a ligne, pour le seul cas legitime : une inclusion placee
# dans une branche de preprocesseur que la cible Windows 95 ne compile jamais.
#
# Le point d'indirection de E02-S02 en est un — il inclut <thread>, <mutex> et
# <condition_variable> dans son `#else`, celui des cibles modernes. Ce
# controleur lit du texte et non l'etat du preprocesseur ; sans derogation il
# refuserait un fichier correct, et l'usage serait alors de le desactiver, ce
# qui coute bien plus cher.
#
# La justification est obligatoire et sa longueur minimale imposee, comme pour
# `exceptions.json` du controle des imports : une derogation sans motif ecrit
# est le debut d'une liste ou l'on fait taire l'outil.
RX_ALLOW = re.compile(r'DKR-WIN95-ALLOW\s*:\s*(.+?)\s*(?:\*/)?\s*$')
ALLOW_MIN_JUSTIFICATION = 30

RED, GREEN, YELLOW, BLUE, OFF = (
    "\033[1;31m", "\033[1;32m", "\033[1;33m", "\033[1;34m", "\033[0m")


def scan(paths, quiet=False):
    """Renvoie la liste des (fichier, ligne, en-tete) fautifs."""
    bad = []
    allowed = []
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
            lines = text.splitlines()
            for i, line in enumerate(lines, 1):
                m = RX_INCLUDE.match(line)
                if not (m and m.group(1) in FORBIDDEN):
                    continue
                # La derogation se porte sur la ligne juste au-dessus.
                allow = RX_ALLOW.search(lines[i - 2]) if i >= 2 else None
                if allow:
                    why = allow.group(1).strip()
                    if len(why) < ALLOW_MIN_JUSTIFICATION:
                        print(f"  {RED}DEROGATION REFUSEE{OFF}  {f}:{i - 1}")
                        print(f"            justification trop courte "
                              f"({len(why)} < {ALLOW_MIN_JUSTIFICATION} caracteres)")
                        bad.append((f, i, m.group(1)))
                    else:
                        allowed.append((f, i, m.group(1), why))
                    continue
                bad.append((f, i, m.group(1)))
    if not quiet:
        print(f"{BLUE}==>{OFF} {files} fichier(s) examine(s)")
        for f, line, header, why in allowed:
            print(f"  {YELLOW}TOLERE{OFF}  {f}:{line} <{header}> — {why}")
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
        # Derogation valable : branche non compilee sur la cible, motif ecrit.
        (tmp / "derogation.cpp").write_text(
            "// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee ici\n"
            "#include <thread>\nint main(){return 0;}\n")
        # Derogation refusee : motif trop court pour dire quoi que ce soit.
        (tmp / "bavarde.cpp").write_text(
            "// DKR-WIN95-ALLOW: parce que\n"
            "#include <thread>\nint main(){return 0;}\n")

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

        print(f"{BLUE}==>{OFF} derogation motivee : branche non compilee")
        bad = scan([tmp / "derogation.cpp"], quiet=True)
        if bad:
            report(bad)
            print(f"{RED}la derogation motivee est refusee{OFF}")
            return 1
        print(f"  {GREEN}acceptee{OFF}")

        print(f"{BLUE}==>{OFF} derogation sans motif serieux")
        bad = scan([tmp / "bavarde.cpp"], quiet=True)
        if not bad:
            print(f"{RED}une derogation sans justification est acceptee — "
                  f"la liste deviendra l'endroit ou l'on fait taire l'outil{OFF}")
            return 1
        print(f"  {GREEN}correctement refusee{OFF}")
    print(f"{BLUE}==>{OFF} {GREEN}le verificateur fonctionne{OFF}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", help="fichiers ou repertoires a examiner")
    ap.add_argument("--self-test", action="store_true",
                    help="eprouver le verificateur par injection")
    # Cliquet, pour les composants dont on sait qu'ils portent encore une dette
    # datee. `ultramodern` en est a une inclusion — <filesystem>, que E02-S05
    # doit retirer. Sans ce reglage, le controle serait soit desactive sur
    # `ultramodern`, soit bloquant a tort ; avec lui, la dette est chiffree,
    # elle ne peut pas grandir, et le jour ou elle tombe a zero le chiffre se
    # met a jour dans le CMake plutot que de rester la sans que personne ne le
    # remarque.
    ap.add_argument("--max", type=int, default=0, metavar="N",
                    help="tolerer au plus N inclusions interdites (defaut 0)")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.paths:
        ap.print_help()
        return 2

    bad = scan(args.paths)
    if bad:
        report(bad)
        if len(bad) <= args.max:
            print(f"  {YELLOW}{len(bad)} inclusion(s) interdite(s){OFF}, "
                  f"tolerees jusqu'a {args.max} — dette connue, elle ne doit pas "
                  f"grandir.")
            return 0
        print(f"  {RED}{len(bad)} inclusion(s) interdite(s){OFF}"
              + (f", au-dela des {args.max} tolerees" if args.max else "")
              + " — voir docs/CPP-SUBSET.md")
        return 1
    if args.max:
        # Le cliquet a fait son office : il faut le desserrer, sinon il cesse
        # de proteger contre la reintroduction.
        print(f"  {GREEN}aucune inclusion interdite{OFF} — la tolerance de "
              f"{args.max} n'a plus lieu d'etre, la retirer du CMake.")
        return 0
    print(f"  {GREEN}aucune inclusion interdite{OFF}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
