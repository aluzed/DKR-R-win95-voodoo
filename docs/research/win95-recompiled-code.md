# Ce que coûte le code recompilé sur la cible Windows 95

Mesures de [E01-S05](../stories/E01-build/E01-S05-compiling-the-recompiled-code.md),
prises sur les sources générées présentes dans le dépôt.

## Le volume

| | |
|---|---:|
| Fichiers `.c` générés | 37 |
| Lignes de C | 587 625 |
| Fonctions `RECOMP_FUNC` | 1 810 |
| dont **feuilles** (aucun appel, aucun saut indirect) | 722 |

## Compilation et édition de liens

Chaîne de E01-S01, `-O2 -march=pentium2 -mno-sse`, sur le poste de build :

| | |
|---|---:|
| Compilation des 37 fichiers, en parallèle | **5,76 s** |
| Pic mémoire d'un processus de compilation | **142 Mo** |
| Archive statique | 4,5 Mo |
| `.text` cumulé des objets | 3,81 Mo |
| PE lié, avec le harnais et les bouchons | **4,23 Mo** |
| `.text` du PE | 3 813 Ko |
| `.rdata` | 37,7 Ko |
| `.data` + `.bss` | 2,8 Ko |

**Rien n'a résisté** : 37 fichiers sur 37, zéro erreur. Le blocage du type
`__int128`, que E00-S03 avait rencontré et corrigé par le patch
`n64recomp/0002`, ne se représente pas.

`RecompiledRSP/aspMain.cpp` compile également — 1,73 s, 33 Ko de `.text` — et
**sans bouchon** : le repli scalaire de `rsp_vu_impl.hpp` fonctionne sans SSE.
Le point 4 du ticket, qui prévoyait d'en poser un et de renvoyer le
comportement à E03-S01, est sans objet. Reste que ce code est 25 fois trop lent
pour le temps réel ([E00-S04](rsp-audio-budget.md)) : c'est E03-S03 qui doit
répondre, pas la compilation.

Les deux garde-fous passent sur le PE final : **aucune instruction hors
Pentium II**, et **chargeable sous Windows 95**.

## L'arithmétique 64 bits passe par libgcc

Le C généré manipule les registres du VR4300 comme des entiers de 64 bits. En
32 bits, les divisions et décalages ne s'inlinent pas : ils appellent les
auxiliaires du compilateur. Sur les 154 symboles que le code générés laisse
indéfinis, on trouve `__divdi3`, `__moddi3`, `__divmoddi4`, `__udivdi3`,
`__umoddi3`, `__ashldi3`, `__lshrdi3`, `__ashrdi3` — **tous fournis par
libgcc**, que l'ADR 0001 lie déjà statiquement.

Les 146 autres sont les auxiliaires de `librecomp` (`__ll_mul_recomp`,
`__osContRamRead_recomp`…) et les crochets du projet. Aucun n'est manquant au
sens propre : ils viennent du runtime, pas du code généré.

Il n'y a donc **aucune extension absente** — c'était la première des trois
questions ouvertes du ticket.

## La comparaison à l'oracle

Le ticket demande mieux qu'une relecture : « un test ciblé sur quelques
fonctions arithmétiques du jeu, comparé à la sortie de l'oracle 64 bits ».

`tools/win95/oracle/` fait cela. 96 fonctions **feuilles** — choisies
automatiquement pour être les plus riches en arithmétique — sont exécutées sur
un état entièrement déterminé : RDRAM remplie par un générateur à graine fixe,
registres tirés du même générateur et pointant dans la zone. L'état final est
résumé par une empreinte FNV-1a.

Deux pièges valaient d'être notés, parce qu'ils rendaient la comparaison
silencieusement creuse :

- **`f_odd` est un pointeur**, pas un tableau. L'empreinte ne peut donc pas
  porter sur la structure brute : sa valeur change à chaque exécution, et
  `sizeof(recomp_context)` **diffère entre 32 et 64 bits**. L'état est résumé
  champ par champ, en largeur fixe.
- Les fonctions qui sortent de la zone corrompaient le tas et tuaient le
  programme bien plus tard, en un endroit sans rapport. La RDRAM est désormais
  **encadrée de pages interdites** — le schéma que `librecomp` emploie pour de
  bon — et la faute devient immédiate et attribuée à la bonne fonction.

### Résultat

L'oracle 64 bits est **déterministe** : trois exécutions, empreintes identiques.
Sur la cible, la comparaison **concorde exactement sur les fonctions
atteintes** — empreintes bit à bit, et jusqu'aux fautes, qui se produisent des
deux côtés sur les mêmes fonctions :

| Fonction | 64 bits | 32 bits |
|---|---|---|
| `func_80014B50` | `4C4E693D94850625` | `4C4E693D94850625` |
| `collision_get_y` | `F1415FD72BFC0752` | `F1415FD72BFC0752` |
| `obj_animate` | `FAULT` | `FAULT` |

### Ce qui empêche d'aller au bout, et pourquoi ce n'est pas un défaut du portage

Le rattrapage des fautes repose sur `signal(SIGSEGV)`. Sur l'hôte il est fiable
— une pile de secours réglant même le débordement de pile, ce qui a demandé
`sigsetjmp` avec sauvegarde du masque, `longjmp` depuis un gestionnaire laissant
autrement le signal bloqué.

**Sous Windows 95 il ne l'est pas.** La faute d'`obj_animate` est bien délivrée ;
celle de `func_8001CD28` ne l'est pas, et le processus meurt. Ce n'est pas un
débordement de pile — une réserve de 64 Mo ne change rien — mais une faute que
le CRT de mingw ne traduit pas en signal sur cette cible.

La comparaison s'arrête donc à la première fonction de ce genre. **Ce que l'on
sait des fonctions atteintes reste vrai** : leur arithmétique 64 bits produit un
état bit à bit identique sur les deux cibles.

Pour aller au bout, la voie est un pilote qui reprend : le programme note la
fonction qu'il s'apprête à exécuter, une relance repart après elle, et la liste
des fonctions non exécutables devient une donnée. Cela fonctionne sur les deux
cibles sans dépendre de ce que Windows 95 veut bien délivrer.

## Reproduire

```sh
# 1. compiler le code recompilé pour la cible
i686-w64-mingw32-gcc-posix -std=c17 -O2 -march=pentium2 -mno-sse \
  -D_WIN32_WINNT=0x0400 -fno-strict-aliasing -c runtime-recomp/RecompiledFuncs/funcs_0.c ...

# 2. les bouchons viennent du verdict du lieur, pas d'une liste devinée
<lien qui échoue> 2>&1 | tools/win95/oracle/generate-stubs.py > stubs.c

# 3. régénérer la sélection de fonctions
tools/win95/oracle/functions.inc   # 96 feuilles, les plus arithmétiques
```

Les empreintes de référence sont dans `tools/win95/oracle/digests-64bit.txt`.
