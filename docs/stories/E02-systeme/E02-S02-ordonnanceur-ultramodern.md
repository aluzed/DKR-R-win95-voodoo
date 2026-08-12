# E02-S02 — Reposer l'ordonnanceur `ultramodern` sur la couche Win95

| | |
|---|---|
| **Épic** | E02 — Substrat système Windows 95 |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E02-S01, E01-S02 |
| **Bloque** | E02-S06, E03-S02 |

## Contexte

`ultramodern` implémente le modèle d'exécution de la N64 : fils du jeu à priorité
stricte, files de messages, événements matériels, et la synchronisation entre le
fil de jeu et le fil graphique. C'est du code que le projet a tout intérêt à
garder — il est éprouvé, et le réécrire reviendrait à refaire le travail
d'`ultramodern` sans son historique de correctifs.

Le dépôt a déjà treize patchs `n64-modern-runtime` en place, dont plusieurs
touchent précisément l'ordonnancement et la remise des tâches graphiques
(`0005-stop-scheduler-cascade-during-quit`,
`0010-yield-between-sp-and-dp-completion`,
`0013-use-reliable-external-message-fifo`). Le précédent est donc établi : ces
composants se patchent, ils ne se réécrivent pas.

Ce ticket substitue la couche de E02-S01 aux primitives standard, sans toucher à
la logique d'ordonnancement.

## Objectif

Faire fonctionner l'ordonnanceur `ultramodern` sur la couche Win95, avec un
comportement identique à celui de l'hôte moderne.

## Périmètre

**Dans :** les patchs `ultramodern` de substitution de primitives, et leur
validation.

**Hors :** toute modification du comportement d'ordonnancement. Une différence
observée est un défaut de portage, pas une amélioration.

## Travail

1. Relever dans `ultramodern` chaque point d'usage des primitives standard :
   création de fil, verrou, attente conditionnelle, sommeil, données locales au
   fil.
2. Introduire une indirection compilée conditionnellement : sur cible moderne,
   les primitives standard ; sur cible Win95, la couche de E02-S01. Cette
   indirection doit être un patch propre, susceptible d'être proposé à l'amont —
   une réécriture invasive est ingérable dans la durée.
3. Traiter le cas du sommeil et des délais. `ultramodern` s'appuie sur des
   attentes temporisées à granularité fine ; sous Windows 95, la granularité par
   défaut de l'ordonnanceur est grossière et se règle par `timeBeginPeriod` de
   `winmm`. Mesurer la granularité réellement atteinte plutôt que la supposer.
4. Traiter l'arrêt. Plusieurs patchs existants portent sur la terminaison propre
   (`0004-wake-game-thread-on-runtime-quit`,
   `0005-stop-scheduler-cascade-during-quit`) : vérifier qu'ils restent corrects
   avec la nouvelle couche, en particulier si l'attente conditionnelle n'offre
   pas la même garantie de réveil.
5. Faire de même pour `librecomp`, dont les fils d'entrées-sorties et d'événements
   utilisent les mêmes primitives.
6. Valider par exécution : lancer le jeu avec le renderer de diagnostic
   (`null_renderer.cpp`) sous Windows 95 émulé, et vérifier qu'il atteint le même
   point que sur l'hôte moderne — mêmes fils créés, mêmes tâches graphiques
   soumises, même cadence.
7. Comparer une trace d'ordonnancement entre les deux cibles : ordre de création
   des fils, ordre de remise des messages, ordre des commutations. C'est cette
   comparaison, et non l'absence de plantage, qui prouve l'équivalence.

## Critères d'acceptation

- [ ] Chaque usage de primitive standard dans `ultramodern` et `librecomp` passe
      par l'indirection.
- [ ] La substitution est un patch sous `patches/n64-modern-runtime/`, référencé
      dans `patches/manifest.json`.
- [ ] Les cibles modernes compilent et se comportent à l'identique avec ce patch
      appliqué.
- [ ] La granularité temporelle réellement atteinte sous Windows 95 est mesurée.
- [ ] Le jeu démarre sous Windows 95 émulé avec le renderer de diagnostic et
      atteint le même point que sur l'hôte moderne.
- [ ] Les traces d'ordonnancement des deux cibles sont comparées et concordent.
- [ ] Les chemins d'arrêt sont revérifiés à la lumière de la nouvelle sémantique
      de réveil.

## Risques

Une divergence subtile d'ordonnancement — un message remis dans un ordre
différent, une commutation qui n'arrive pas au même moment — peut ne rien casser
visiblement et corrompre le déterminisme du jeu. La comparaison de traces de
l'étape 7 est le seul moyen de l'attraper avant qu'elle ne devienne un bug de
gameplay difficile à cerner.

## Références

- `patches/n64-modern-runtime/` — treize patchs, dont plusieurs sur
  l'ordonnancement
- `runtime-recomp/src/game/null_renderer.cpp`
- E02-S01 — couche de fils et synchronisation
