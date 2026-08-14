#!/usr/bin/env python3
"""E04-S06 — genere les vecteurs de test du decodeur de combineur.

Les vecteurs viennent des **en-tetes de la decomposition** — les macros
`G_CC_*`, `G_CCMUX_*` et `G_ACMUX_*` — et non d'une transcription a la main.
Une transcription se trompe silencieusement et fait alors passer un decodeur
faux : le test et le code partageraient la meme erreur.

    tools/win95/gen_combiner_vectors.py [racine-decomp] > vectors.inc

La racine par defaut est le portage natif voisin. Ce depot-ci n'a pas ces
sources : il travaille depuis du MIPS recompile, ce qui est precisement la
raison pour laquelle l'inventaire de E04-S06 ne peut pas y etre reverifie par la
meme methode.
"""
import re, sys, pathlib

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/var/www/Diddy-Kong-Racing")

def read_defines(pattern):
    out = {}
    for header in root.glob("include/**/*.h"):
        try:
            text = header.read_text(errors="ignore")
        except OSError:
            continue
        for m in re.finditer(pattern, text, re.M):
            out.setdefault(m.group(1), int(m.group(2)))
    return out

ccmux = read_defines(r'^#define\s+G_CCMUX_(\w+)\s+(\d+)')
acmux = read_defines(r'^#define\s+G_ACMUX_(\w+)\s+(\d+)')
if not ccmux or not acmux:
    sys.exit(f"erreur : G_CCMUX/G_ACMUX introuvables sous {root}")

combiners = {}
for header in root.glob("include/**/*.h"):
    try:
        text = header.read_text(errors="ignore")
    except OSError:
        continue
    for m in re.finditer(r'^#define\s+G_CC_(\w+)\s+(.+)$', text, re.M):
        args = [a.strip() for a in m.group(2).split(',')]
        if len(args) == 8:
            combiners[m.group(1)] = args

def shiftl(v, s, w):
    return (v & ((1 << w) - 1)) << s

def pack(args):
    """Les quatre macros GCCc*w* de gbi.h, appliquees telles quelles.

    **Les largeurs de champ ne sont pas uniformes, et cela change la valeur.**
    `G_CCMUX_0` vaut 31, ce qui tient dans les cinq bits du champ `c` mais pas
    dans les quatre bits de `a` et `b` : le materiel y range 15. Un decodeur ne
    peut donc pas rendre 31 pour ces positions, et l'attendre serait exiger
    l'impossible.

    Une premiere version de ce generateur emettait la valeur brute, et le test
    accusait le decodeur — qui avait raison. Les valeurs attendues sont donc
    tronquees ici comme le materiel les tronque.

    Consequence a retenir pour E05-S03 : **le meme `0` symbolique ne s'encode
    pas de la meme facon selon la position**, et une table de correspondance
    ecrite sur les noms plutot que sur les valeurs se tromperait.
    """
    a, b, c, d, Aa, Ab, Ac, Ad = args
    A, B, C, D = ccmux[a] & 0xF, ccmux[b] & 0xF, ccmux[c] & 0x1F, ccmux[d] & 0x7
    AA, AB = acmux[Aa] & 0x7, acmux[Ab] & 0x7
    AC, AD = acmux[Ac] & 0x7, acmux[Ad] & 0x7
    # gsDPSetCombineMode(x, x) : le meme jeu pour les deux cycles.
    w0 = (shiftl(A, 20, 4) | shiftl(C, 15, 5) | shiftl(AA, 12, 3) | shiftl(AC, 9, 3)
          | shiftl(A, 5, 4) | shiftl(C, 0, 5))
    w1 = (shiftl(B, 28, 4) | shiftl(B, 24, 4) | shiftl(AA, 21, 3) | shiftl(AC, 18, 3)
          | shiftl(D, 15, 3) | shiftl(AB, 12, 3) | shiftl(AD, 9, 3)
          | shiftl(D, 6, 3) | shiftl(AB, 3, 3) | shiftl(AD, 0, 3))
    return w0, w1, (A, B, C, D, AA, AB, AC, AD)

print("/* Genere par tools/win95/gen_combiner_vectors.py — ne pas editer. */")
print(f"/* Source : {root}/include — {len(combiners)} macros G_CC_*. */")
print("typedef struct {")
print("    const char *name;")
print("    unsigned int w0, w1;")
print("    unsigned char a, b, c, d, Aa, Ab, Ac, Ad;")
print("} combiner_vector;")
print()
print("static const combiner_vector COMBINER_VECTORS[] = {")
for name in sorted(combiners):
    try:
        w0, w1, f = pack(combiners[name])
    except KeyError as e:
        print(f"    /* {name} : entree inconnue {e} — ignoree */")
        continue
    print(f'    {{ "G_CC_{name}", 0x{w0:08X}u, 0x{w1:08X}u, '
          f'{f[0]}, {f[1]}, {f[2]}, {f[3]}, {f[4]}, {f[5]}, {f[6]}, {f[7]} }},')
print("};")
