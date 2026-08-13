#!/usr/bin/env python3
"""E02-S01 — recense les exports de Windows 95 qui ne font rien.

    tools/win95/find_stubs.py KERNEL32.DLL USER32.DLL ...
    tools/win95/find_stubs.py --write KERNEL32.DLL      # met a jour exports/stubs/

Une API absente de la table d'exports est un probleme *bruyant* : Windows 95
refuse de charger le programme et nomme le symbole. C'est ce que verifie
`check_imports.py`.

Une API **exportee mais vide** est un probleme silencieux, et donc pire. Le lien
reussit, le chargement reussit, le controle d'imports est satisfait — et la
fonction ne fait rien. C'est ainsi que `CreateSemaphoreW` a failli emporter tout
le planificateur d'`ultramodern` : `moodycamel::LightweightSemaphore` l'appelle,
recoit un descripteur nul, et ni son attente ni son signal ne fonctionnent
ensuite. Voir docs/research/win95-blockers.md.

## Comment un bouchon se reconnait

Les entrees vides de Windows 95 partagent une forme fixe, qu'on peut donc
reconnaitre mecaniquement plutot que de les deviner :

    33 c0              xor  eax,eax     ; valeur de retour = 0 (echec)
    b1 XX              mov  cl,index    ; numero du bouchon
    e9 XX XX XX XX     jmp  queue       ; queue commune

et la queue commune pose `ERROR_CALL_NOT_IMPLEMENTED` (120) par `SetLastError`.

Le motif est reconnu ici sur les neuf premiers octets du code de chaque export
nomme. Mais « le motif ressemble a un bouchon » ne serait qu'une impression, et
un faux positif ferait echouer le build de tout le monde : le releve est donc
**verifie**, pas seulement reconnu.

La verification est la convergence des sauts. Les bouchons d'une meme DLL
sautent tous a une unique queue commune — 179 vers `0x1319` pour KERNEL32, 162
vers `0x62c6` pour USER32, 62 vers `0x98ea` pour GDI32, 176 vers `0x1356` pour
ADVAPI32. Une seule adresse par DLL, sans exception. L'outil l'exige et refuse
d'ecrire un releve qui ne la presenterait pas.

Preuve supplementaire quand on en veut une : plusieurs bouchons **partagent la
meme adresse d'entree**. `LoadLibraryExW` et `MoveFileExW` sont a la meme,
`CreateEventW` et `CreateSemaphoreW` aussi. Deux fonctions au comportement
radicalement different ne partagent du code que lorsqu'aucune des deux n'en a.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from pe_symbols import PE                                    # noqa: E402

STUBS_DIR = pathlib.Path(__file__).resolve().parent / "exports" / "stubs"

RED, GREEN, YELLOW, OFF = "\033[1;31m", "\033[1;32m", "\033[1;33m", "\033[0m"


def find_stubs(path):
    """Rend ([(nom, rva, cible)], nombre d'exports nommes).

    `cible` est l'adresse ou saute le bouchon. Elle est relevee et non ignoree :
    c'est elle qui transforme « le motif ressemble a un bouchon » en « les N
    bouchons de cette DLL sautent tous au meme endroit », c'est-a-dire en une
    verification plutot qu'en une ressemblance. Voir `main`.
    """
    pe = PE(str(path))
    data, ddir = pe.d, pe.ddir

    rva, _ = struct.unpack_from("<II", data, ddir)
    if not rva:
        return [], 0
    e = pe.off(rva)
    n_functions, n_names = struct.unpack_from("<II", data, e + 0x14)
    a_functions, a_names, a_ordinals = struct.unpack_from("<III", data, e + 0x1C)
    off_f, off_n, off_o = pe.off(a_functions), pe.off(a_names), pe.off(a_ordinals)

    stubs = []
    for i in range(n_names):
        name = pe.cstr(pe.off(struct.unpack_from("<I", data, off_n + 4 * i)[0]))
        ordinal = struct.unpack_from("<H", data, off_o + 2 * i)[0]
        if ordinal >= n_functions:
            # Table tronquee ou illisible : le dire, plutot que lire a cote.
            print(f"  {YELLOW}ignore{OFF} {name} : ordinal {ordinal} hors des "
                  f"{n_functions} entrees")
            continue
        func_rva = struct.unpack_from("<I", data, off_f + 4 * ordinal)[0]
        off = pe.off(func_rva)
        if off is None:
            continue
        code = data[off:off + 9]
        # xor eax,eax ; mov cl,imm8 ; jmp rel32
        if (len(code) >= 9 and code[0:2] == b"\x33\xc0"
                and code[2] == 0xB1 and code[4] == 0xE9):
            rel = struct.unpack_from("<i", code, 5)[0]
            stubs.append((name, func_rva, func_rva + 9 + rel))
    return stubs, n_names


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dlls", nargs="+", help="DLL extraites de la machine cible")
    ap.add_argument("--write", action="store_true",
                    help=f"ecrit le releve dans {STUBS_DIR}")
    args = ap.parse_args(argv)

    status = 0
    for dll in args.dlls:
        path = pathlib.Path(dll)
        stem = path.stem.upper()
        stubs, total = find_stubs(path)

        share = (100.0 * len(stubs) / total) if total else 0.0
        colour = RED if share > 50 else (YELLOW if stubs else GREEN)
        print(f"{colour}{stem}{OFF} : {len(stubs)} bouchons sur {total} "
              f"exports nommes ({share:.0f} %)")

        # La convergence des sauts est ce qui rend le releve digne de confiance.
        # Un `BOUCHON` fait echouer le build (check_imports.py) : un seul faux
        # positif bloquerait tout le monde. Sur les DLL de la machine de test,
        # les bouchons d'une meme DLL sautent **tous** a une unique adresse —
        # 179 vers 0x1319 pour KERNEL32, 162 vers 0x62c6 pour USER32. Exiger
        # cette convergence transforme l'argument « aucun faux positif
        # plausible » en propriete verifiee.
        targets = sorted({t for _, _, t in stubs})
        if len(targets) > 1:
            status = 1
            print(f"  {RED}REFUSE{OFF} : {len(targets)} cibles de saut "
                  f"distinctes ({', '.join(hex(t) for t in targets)})")
            print(f"          Le motif ne designe donc plus une queue unique, "
                  f"et le releve n'est plus sur.")
            print(f"          Ne rien ecrire : verifier au desassemblage avant "
                  f"de faire confiance a cette liste.")
            continue
        if targets:
            print(f"  queue commune : {hex(targets[0])}")

        if args.write:
            STUBS_DIR.mkdir(parents=True, exist_ok=True)
            target = STUBS_DIR / f"{stem}.txt"
            if stubs:
                target.write_text("".join(f"{n}\n" for n, _, _ in sorted(stubs)))
                print(f"  ecrit {target}")
            elif target.exists():
                target.unlink()
                print(f"  supprime {target} (aucun bouchon)")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
