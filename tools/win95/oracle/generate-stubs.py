#!/usr/bin/env python3
"""E01-S05 — fabrique les bouchons dont le harnais d'oracle a besoin.

    <lien qui echoue> 2>&1 | tools/win95/oracle/generate-stubs.py > stubs.c

Le harnais execute des fonctions du jeu **isolement**, sans le reste du
runtime. Il faut donc satisfaire l'editeur de liens pour tout ce que le code
recompile appelle et que personne ne fournit ici : les auxiliaires de
`librecomp`, les crochets du projet, les fonctions du systeme N64.

L'entree est la **sortie d'erreur du lieur**, et non un relevé de symboles.
C'est lui qui fait autorite : il a deja consulte la libc, libm et libgcc, et ce
qu'il declare indefini l'est reellement. Partir de `nm` obligerait a deviner
quelles bibliotheques fournissent quoi — et la premiere version de ce script,
qui le faisait, bouchonnait `lrintf` et `__fprintf_chk`.

Les bouchons sont **generes** et non ecrits a la main, pour deux raisons. La
premiere est qu'ils suivent alors la regeneration du code : une fonction qui
apparait n'est pas oubliee. La seconde est qu'un bouchon ecrit a la main finit
par contenir une implementation, et le jour ou elle est fausse, personne ne
cherche la.

Chacun **avorte** au lieu de rendre une valeur. C'est la propriete qui compte :
si une fonction soumise a la comparaison atteignait un bouchon, l'empreinte
comparerait deux fois du vide et la comparaison serait creuse sans le dire. Un
arret bruyant vaut mieux qu'un accord obtenu par accident.
"""
import re
import sys


# GNU ld, en anglais comme en francais.
RX_UNDEFINED = re.compile(
    r"undefined reference to [`\u2018']([^'\u2019]+)|"
    r"r\u00e9f\u00e9rence ind\u00e9finie vers \u00ab\s*([^\u00bb]+?)\s*\u00bb")


def main(argv):
    del argv
    missing = set()
    for line in sys.stdin:
        for m in RX_UNDEFINED.finditer(line):
            missing.add((m.group(1) or m.group(2)).strip())
    missing = sorted(missing)

    print("/* Genere par tools/win95/oracle/generate-stubs.py — ne pas editer. */")
    print("#include <stdio.h>")
    print("#include <stdlib.h>")
    print("#include <stdint.h>")
    print()
    print("static void stub_reached(const char *name)")
    print("{")
    print("    /* Un bouchon atteint rendrait la comparaison creuse : deux cibles")
    print("       s'accorderaient sur du vide. On s'arrete plutot que de le taire. */")
    print('    fprintf(stderr, "\\nHARNAIS INVALIDE : appel a %s\\n", name);')
    print("    abort();")
    print("}")
    print()
    for name in missing:
        print(f'void {name}(void) {{ stub_reached("{name}"); }}')
    print(f"\n/* {len(missing)} bouchon(s). */", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
