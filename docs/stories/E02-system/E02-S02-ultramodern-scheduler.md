# E02-S02 — Reposer l'ordonnanceur `ultramodern` sur la couche Win95

| | |
|---|---|
| **Épic** | E02 — Substrat système Windows 95 |
| **Statut** | IN_PROGRESS |
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

- [~] Chaque usage de primitive standard dans `ultramodern` et `librecomp` passe
      par l'indirection. **`ultramodern` : fait** (5 primitives, 7 fichiers).
      `librecomp` : non, il est bloqué par ailleurs.
- [x] La substitution est un patch sous `patches/n64-modern-runtime/`, référencé
      dans `patches/manifest.json` — 0015, avec son empreinte.
- [x] Les cibles modernes compilent et se comportent à l'identique avec ce patch
      appliqué — 15 unités de traduction compilées sur l'hôte Linux 64 bits avant
      et après ; sans les deux macros, le patch se réduit à des
      `using std::...`.
- [ ] La granularité temporelle réellement atteinte sous Windows 95 est mesurée.
- [ ] Le jeu démarre sous Windows 95 émulé avec le renderer de diagnostic et
      atteint le même point que sur l'hôte moderne.
- [ ] Les traces d'ordonnancement des deux cibles sont comparées et concordent.
- [ ] Les chemins d'arrêt sont revérifiés à la lumière de la nouvelle sémantique
      de réveil.

## État au 2026-08-13 — la substitution est faite, l'exécution reste bloquée

Le patch **0015**, `platform-seam-for-threading-primitives`, route les cinq
primitives d'`ultramodern` — `thread`, `mutex`, `condition_variable`,
`lock_guard`, `unique_lock` — par un point d'indirection que la cible remplit
avec la couche de E02-S01. Aucune logique d'ordonnancement n'est touchée : le
patch ne fait que renommer des types.

Ce qui est acquis, et vérifié :

| | |
|---|---|
| `ultramodern` compile pour Windows 95 | **15 fichiers sur 15** |
| Les cibles modernes compilent à l'identique | 15 sur 15, branche `std::` inchangée |
| Inclusions interdites | **9 → 1**, la dernière étant `<filesystem>` (E02-S05) |
| Le pont C++ sur la machine | 22 contrôles, 0 échec |
| `ultramodern` dans le build de la cible | bibliothèque `win95ultramodern` |

Deux découvertes ont réduit le travail annoncé :

- **`thread_local` fonctionne sous Windows 95.** Le répertoire TLS du PE y est
  bien traité, contrairement à ce qui se dit souvent. Mesuré sur la machine par
  `tools/win95/witnesses/tls_probe.cpp` : deux fils, valeurs isolées. Les trois
  `thread_local` de `threads.cpp` n'ont donc rien demandé.
- **Le point 3 du travail est sans objet.** `timer.cpp` a déjà une branche
  `#ifdef _WIN32` qui appelle `Sleep` directement ; `std::this_thread` n'est
  jamais atteint sur cette cible, et la question de `timeBeginPeriod` appartient
  à E02-S03, qui traite la cadence.

**Ce qui reste bloqué, et par quoi.** Les points 5 à 7 — `librecomp`, lancer le
jeu, comparer les traces d'ordonnancement — supposent que le jeu se lie pour
cette cible. Il ne le peut pas : `librecomp` porte encore `<filesystem>`
(E02-S05) et six erreurs de compilation, le code recompilé attend
[E01-S05](../E01-build/E01-S05-compiling-the-recompiled-code.md), et SDL2 attend
[E07-S03](../E07-scope/E07-S03-sdl2-decoupling.md). Ce ticket ne peut pas se
fermer avant eux.

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
