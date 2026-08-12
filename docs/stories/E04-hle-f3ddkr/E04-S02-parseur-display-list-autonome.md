# E04-S02 — Parseur de display list F3DDKR autonome

| | |
|---|---|
| **Épic** | E04 — HLE F3DDKR indépendant de RT64 |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E04-S01 |
| **Bloque** | E04-S03, E04-S05, E04-S06, E04-S08 |

## Contexte

`F3DDKRRT64Bridge` sait déjà décoder le microcode de Rare : ses quatorze
gestionnaires couvrent l'ensemble du jeu de commandes utilisé par DKR. C'est un
travail précieux, validé par un portage qui tourne, et il ne faut surtout pas le
refaire de zéro.

Mais il est écrit contre RT64 : il enregistre ses gestionnaires dans un
`RT64::GBI`, reçoit des `RT64::DisplayList**`, écrit dans un `RT64::State`. Le
travail consiste donc à en extraire la logique de décodage — qui est propre au
microcode et n'a rien à voir avec RT64 — et à la reposer sur l'interface de
E04-S01.

Un point d'attention hérité, à ne pas perdre au passage : le décodeur actuel
**valide toutes les plages** avant de les utiliser — matrices, sommets, triangles,
textures, display lists imbriquées — et rejette les données invalides par une
erreur bornée plutôt que de laisser adresser la mémoire hôte (`docs/F3DDKR.md`).
Cette discipline doit survivre à l'extraction : elle protège contre une ROM
modifiée comme contre un bug du portage.

## Objectif

Livrer `platform/render/f3ddkr.{h,cpp}` : un décodeur de display list F3DDKR qui
ne dépend que de l'interface de E04-S01.

## Périmètre

**Dans :** l'analyse de la display list, la répartition des commandes, la
validation des plages, la gestion des display lists imbriquées.

**Hors :** la transformation des sommets (E04-S03), le découpage (E04-S05), l'état
RDP (E04-S06), les textures (E04-S07).

## Travail

1. Cartographier les quatorze commandes à partir de `f3ddkr_rt64.cpp` : opcode,
   disposition des champs, effet. Documenter le tout dans
   `docs/research/f3ddkr-commands.md` — cette cartographie a une valeur propre,
   indépendamment du portage.
2. Extraire la logique de décodage vers le nouveau module, en remplaçant chaque
   écriture dans `RT64::State` par un appel à l'interface de E04-S01 ou par une
   écriture dans l'état interne du décodeur.
3. Reprendre intégralement la validation des plages. Chaque lecture depuis
   l'instantané RDRAM est bornée, et une plage invalide produit une erreur
   circonscrite qui interrompt la display list courante sans faire tomber le jeu.
4. Traiter les display lists imbriquées : `DisplayListBranch`, `EndDisplayList` et
   `CountedDisplayList`. Borner la profondeur d'imbrication et vérifier que le
   dépassement est traité proprement.
5. Traiter `DMAOffsets`, spécificité du microcode de Rare : les sommets et les
   matrices sont adressés par des décalages relatifs à des bases fournies par la
   commande. C'est le mécanisme central de F3DDKR, et une erreur de base y produit
   une géométrie totalement absurde.
6. Écrire le mode trace : un mode qui journalise chaque commande décodée, ses
   paramètres et les primitives émises. Sans cet outil, tout diagnostic graphique
   sur la machine cible se fait à l'aveugle.
7. Écrire les tests sur des display lists capturées depuis de vraies parties :
   écran-titre, sélection de personnage, deux niveaux de nature différente. Le
   test vérifie la séquence de primitives émises, pas seulement l'absence de
   plantage.
8. Prévoir la capture de ces display lists depuis la cible moderne, et leur
   rejeu — c'est le socle du harnais de comparaison de E09-S02.

## Critères d'acceptation

- [ ] `platform/render/f3ddkr.{h,cpp}` ne référence aucun type RT64.
- [ ] Les quatorze commandes sont décodées et documentées dans
      `docs/research/f3ddkr-commands.md`.
- [ ] La validation de plages est intégralement reprise et testée par injection de
      display lists volontairement corrompues.
- [ ] L'imbrication est bornée, et le dépassement produit une erreur circonscrite.
- [ ] Le mode trace journalise commandes et primitives émises.
- [ ] Les tests rejouent au moins quatre display lists capturées et vérifient la
      séquence de primitives.
- [ ] La séquence de primitives est identique à celle produite par le décodeur RT64
      sur les mêmes entrées, pour tout ce qui ne dépend pas des étages suivants.

## Risques

L'extraction est une réécriture déguisée, et une réécriture perd des correctifs
subtils si elle procède par relecture. Procéder par déplacement de code plutôt que
par réimplémentation, et valider par comparaison de séquences plutôt que par
inspection.

## Références

- `runtime-recomp/src/game/f3ddkr_rt64.cpp:1-...` — décodeur actuel
- `runtime-recomp/src/game/f3ddkr_rt64.hpp` — liste des gestionnaires
- `docs/F3DDKR.md` — validation des plages, groupes de présentation
- `extern/dkr-decomp` — `include/f3ddkr.h` documente le microcode côté decomp
