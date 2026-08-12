# E00-S02 — Spike : produire un exécutable qui démarre sous Windows 95

| | |
|---|---|
| **Épic** | E00 — Cadrage, mesures et décisions |
| **Statut** | REVIEW |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E00-S01 |
| **Bloque** | E01-S01, E01-S02, E01-S03 |

## État au 2026-08-12 — tranché, et le risque majeur est écarté

ADR : [`docs/adr/0001-toolchain.md`](../../adr/0001-toolchain.md).

**Décision : mingw-w64 GCC 13, `i686-w64-mingw32`, modèle de threads `posix`,
CRT lié statiquement, avec le pont `tools/win95/win95compat/`.**

| Candidat | C++ | T1 | T2 | T3a | T3b | Taille T3b |
|---|---|---|---|---|---|---:|
| Open Watcom 2.0 | C++98 partiel | ✅ | ✅ | *sans objet* | ✅ | 51 200 o |
| mingw GCC 13, posix | **C++20** | ✅ | ✅ | ❌ | ✅ *(avec pont)* | 501 625 o |
| mingw GCC 13, win32 | **C++20** | ✅ | ✅ | ❌ | ❌ | 353 108 o |

Aucun binaire n'émet d'instruction SSE, bibliothèque standard comprise.

**Le risque majeur du ticket ne s'est pas matérialisé** : `ultramodern` et
`librecomp` restent patchables. Watcom passe pourtant tous les témoins, avec des
binaires dix fois plus petits et sans pont — mais son C++98 imposerait de
réécrire les deux bibliothèques. Il reste le repli documenté si le portage
dérape.

**Trois résultats qui changent la suite :**

1. **`std::thread` ne fonctionne pas sur la cible, même une fois tous les
   symboles fournis.** T3a se charge, démarre, puis échoue dans la partie
   threads. **E02-S01 devient obligatoire**, et ce n'est plus une hypothèse.
2. **On ne peut pas éviter le problème en évitant `std::thread`** : T3b, qui
   n'utilise que `CreateThread`, échoue quand même au chargement — `libstdc++`
   importe `GetThreadId` pour les exceptions et le RTTI. La machine le dit
   elle-même : « lié à une exportation manquante KERNEL32.DLL:GetThreadId ».
3. **Un bouchon licite peut figer la machine.** `TryEnterCriticalSection`
   renvoyant toujours `FALSE` — réponse permise par le contrat — a bloqué
   Windows 95 au point d'arrêter l'horloge : `winpthreads` boucle dessus. Le pont
   implémente les cinq fonctions de section critique.

Le pont est écrit, lié et **éprouvé sur la machine** : T3b passe de « ne démarre
pas » à 1000/1000, RTTI et exceptions compris. Il est le point de départ de
E02-S01.

## Contexte

Le choix du compilateur commande le langage disponible, et le langage disponible
commande la quantité de code à réécrire. C'est la décision la plus structurante
du projet, et elle se tranche par l'expérience, pas par la lecture.

Trois familles de candidats, avec un compromis franc entre modernité du langage
et compatibilité de la cible :

| Candidat | C++ disponible | Compatibilité Win95 |
|---|---|---|
| Visual C++ 6.0 / 2003 (7.1) | C++98 | native, dernière génération Microsoft à cibler 95 |
| Open Watcom 2.0 | C99 / C++98 partiel | native, encore maintenue |
| clang ou GCC récent → `i686-w64-mingw32` | C++17 / C++20 | à prouver — le démarrage du CRT mingw-w64 appelle des API postérieures à 95 |

La troisième voie est la seule qui préserve `ultramodern` et `librecomp` sans
réécriture ; c'est aussi la seule dont la compatibilité n'est pas acquise.

## Objectif

Faire démarrer sous Windows 95 un exécutable témoin de complexité croissante, et
en déduire quelle toolchain le projet adopte.

## Périmètre

**Dans :** trois exécutables témoins, mesurés dans un Windows 95 émulé.

**Hors :** compiler quoi que ce soit du jeu (c'est E01-S05).

## Travail

1. Monter la machine de test (E09-S01 en fournit la recette ; si elle n'est pas
   prête, un Windows 95 OSR2.5 sous 86Box suffit à ce spike).
2. Pour chaque candidat, produire trois témoins :
   - **T1** — `MessageBoxA` et sortie. Prouve le format PE, le sous-système et
     les imports de base.
   - **T2** — T1 plus le CRT : allocation tas, `fopen`/`fread`, `printf`,
     mathématiques flottantes. Prouve la dépendance au CRT et sa distribution.
     Attention : `msvcrt.dll` n'est pas présent dans le Windows 95 de première
     génération — statuer entre édition de liens statique et redistribution.
   - **T3** — T2 plus deux threads, un objet de synchronisation, et une classe
     C++ avec exceptions et RTTI. Prouve le modèle d'exécution, qui est ce dont
     `ultramodern` a besoin.
3. Pour le candidat mingw-w64, forcer `-march=pentium2 -mtune=pentium3
   -mfpmath=387 -mno-sse` et vérifier dans le désassemblage qu'**aucune**
   instruction SSE ne subsiste, y compris dans le code de démarrage et dans les
   fonctions de la bibliothèque standard.
4. Relever pour chaque témoin qui passe : taille du binaire, liste des DLL
   importées, liste des symboles importés, et le comportement observé au
   démarrage sous 95.
5. Pour chaque témoin qui échoue, relever le symbole ou l'instruction exacte en
   cause. Un échec documenté vaut mieux qu'un succès inexpliqué.

## Critères d'acceptation

- [ ] Les trois témoins sont construits par au moins deux candidats.
- [ ] Au moins un candidat exécute T3 sous Windows 95 émulé, capture d'écran à
      l'appui.
- [ ] Le tableau des résultats donne, par candidat : C++ disponible, taille de
      T3, DLL et symboles importés, statut d'exécution.
- [ ] Les échecs nomment le symbole ou l'instruction fautive.
- [ ] Une ADR de choix de toolchain est rédigée (`docs/adr/0001-toolchain.md`),
      qui tranche également le mode de distribution du CRT.
- [ ] L'ADR indique la conséquence directe du choix sur `ultramodern` et
      `librecomp` : patchables tels quels, ou à réécrire — en s'appuyant sur le
      compte de constructions C++20 de E00-S01.

## Risques

Si aucun compilateur moderne ne produit un binaire viable sous 95, le projet
retombe sur C++98, et `ultramodern`/`librecomp` deviennent une réécriture
complète — plusieurs semaines qui ne figurent aujourd'hui dans aucun ticket.
C'est précisément pour cela que ce spike passe avant tout le reste. Le résultat
doit remonter en risque projet, pas rester dans le fichier d'ADR.

## Références

- `docs/BUILDING.md` — chaîne de build actuelle (VS 2022, CMake, Ninja)
- `runtime-recomp/CMakeLists.txt:26-31`
- E00-S01 — inventaire des blocages
