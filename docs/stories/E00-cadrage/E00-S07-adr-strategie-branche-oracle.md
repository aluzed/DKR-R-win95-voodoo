# E00-S07 — ADR : stratégie de dépôt et oracle de référence

| | |
|---|---|
| **Épic** | E00 — Cadrage, mesures et décisions |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | S |
| **Dépend de** | — |
| **Bloque** | E07-S01, E09-S02, E09-S03 |

## Contexte

Ce dépôt est un fork de DKR-R. Le portage Win95 va retirer des fonctionnalités
(widescreen, interpolation, texture packs, overlay ImGui), remplacer des
dépendances entières (SDL2, RT64) et abaisser la norme du langage. Ces
changements sont incompatibles avec l'amont : il n'y a pas de retour possible.

Il faut donc décider, une fois pour toutes, deux choses distinctes :

1. **Le devenir du portage moderne dans ce dépôt.** Le supprimer simplifie
   énormément le code. Le conserver coûte de la maintenance — mais fournit
   l'unique référence exécutable permettant de savoir si le rendu Glide est
   *juste*. Sans elle, la seule référence est un émulateur N64 tiers ou la
   console, et la comparaison image par image devient beaucoup plus lourde.
2. **Le suivi de l'amont.** Reprendre les corrections de DKR-R, ou figer.

Le portage natif voisin (`/var/www/Diddy-Kong-Racing`) a rencontré exactement
cette question et l'a tranchée dans son ADR 0003 : fork dédié, matching abandonné
comme contrainte de livraison, mais **build de référence conservé comme oracle de
test**. La justification y est explicite — casser l'oracle, c'est perdre le seul
moyen de savoir si le portage est correct.

## Objectif

Écrire `docs/adr/0004-strategie-depot.md` : ce qui est conservé, ce qui est
retiré, ce qui reste constructible, et comment l'oracle est utilisé.

## Périmètre

**Dans :** la décision, et la structure de build qu'elle impose.

**Hors :** la mise en œuvre de la réduction de périmètre (E07).

## Travail

1. Trancher la conservation du chemin RT64 / SDL2. Trois options honnêtes :
   - **supprimer** — code minimal, plus d'oracle exécutable ;
   - **conserver dans le même arbre** derrière un interrupteur CMake, les deux
     backends implémentant `ultramodern::renderer::RendererContext` — cette
     interface existe déjà et porte trois implémentations potentielles ;
   - **conserver sur une branche séparée** — arbre propre, mais dérive garantie
     et comparaison plus laborieuse.

   La deuxième option est la seule qui rende E09-S02 réellement praticable, parce
   qu'elle permet de rejouer *la même trace de display list* dans les deux
   backends depuis un seul binaire de développement.
2. Définir l'interrupteur de build et son défaut. La cible Win95 ne doit
   évidemment jamais tenter de compiler RT64 : `DKR_RUNTIME_BUILD_RT64` existe
   déjà et vaut `OFF` par défaut — vérifier qu'il suffit et qu'aucun code de
   `src/game/` ne référence RT64 inconditionnellement.
3. Trancher le suivi de l'amont : version de DKR-R figée comme base, et procédure
   de reprise sélective des correctifs, ou gel complet.
4. Statuer sur le pipeline de patchs. La règle du dépôt est ferme
   (`docs/ARCHITECTURE.md`) : jamais de modification directe des worktrees de
   dépendances. Le portage va exiger des patchs lourds sur `ultramodern` et
   `librecomp` — confirmer que `patches/manifest.json` reste la voie unique, et
   fixer la limite au-delà de laquelle une dépendance est forkée plutôt que
   patchée.
5. Trancher le sort des tests existants. Les 18 suites de `runtime-recomp/tests/`
   portent sur des politiques modernes ; certaines deviennent sans objet avec le
   profil « Accurate » seul, d'autres restent valables (codec de sauvegarde,
   égaliseur audio, gestionnaire de sauvegardes). Trier.

## Critères d'acceptation

- [ ] `docs/adr/0004-strategie-depot.md` tranche les cinq points.
- [ ] La décision sur l'oracle est justifiée par son usage concret en E09-S02.
- [ ] L'interrupteur de build et son défaut sont nommés, et la vérification qu'il
      suffit à exclure RT64 est faite.
- [ ] La limite patch / fork des dépendances est écrite.
- [ ] Le tri des suites de tests existantes est fait, suite par suite.

## Risques

Conserver deux backends double le coût de chaque changement d'interface de
rendu. Ce coût est réel et doit être accepté en connaissance de cause, pas subi :
si l'ADR retient la conservation, elle doit dire jusqu'à quand.

## Références

- `docs/ARCHITECTURE.md` — frontières protégées, pipeline de patchs
- `runtime-recomp/CMakeLists.txt:36-37` — `DKR_RUNTIME_BUILD_RT64`
- `runtime-recomp/src/game/null_renderer.hpp` — troisième implémentation possible
- `../../Diddy-Kong-Racing/docs/adr/0003-strategie-fork.md` — même arbitrage,
  déjà tranché côté portage natif
