# ADR 0004 — Stratégie de dépôt et oracle de référence

- **Statut** : accepté
- **Date** : 2026-08-12
- **Ticket** : [E00-S07](../stories/E00-scoping/E00-S07-adr-oracle-branch-strategy.md)

## Contexte

Ce dépôt est un fork de [`ThatGuyMcd/DKR-R`](https://github.com/ThatGuyMcd/DKR-R),
détaché au commit `e5d1bbb`. Le portage Windows 95 / 3dfx retire des
fonctionnalités, remplace des dépendances entières et vise une machine que
l'amont n'a jamais envisagée. Il fallait décider, une fois, ce qui reste
constructible et comment on saura que le rendu Glide est *juste*.

Deux mesures ont changé la nature de la question, et elles doivent être posées
avant la décision.

**Le dépôt est déjà structuré pour deux cibles.** `runtime-recomp/CMakeLists.txt`
porte l'option `DKR_RUNTIME_BUILD_RT64`, à `OFF` par défaut, qui propage le
symbole `DKR_RUNTIME_HAS_RT64`. [E00-S01](../research/win95-blockers.md) a
vérifié ce que cet interrupteur couvre réellement, en suivant l'imbrication des
directives du préprocesseur plutôt qu'en cherchant des motifs :

| Composant | Références hors garde |
|---|---:|
| RT64 | **0** — `f3ddkr_rt64.hpp` ne fait que des déclarations anticipées |
| Dear ImGui | **0** — les 26 lignes de `runtime_platform.cpp` sont gardées, `imgui.h` compris |
| SDL2 | **4**, dans deux fichiers |
| Texture packs, overlay CRT, import Rice | **0** — fichiers entiers exclus |

La séparation entre « chemin moderne » et « socle portable » n'est donc pas à
inventer : elle existe, elle est appliquée par le build, et elle laisse quatre
références à traiter.

**Les tests sont portables.** Les 18 suites de `runtime-recomp/tests/` testent
chacune une politique en en-tête, sous `if(BUILD_TESTING)` et non sous
`DKR_RUNTIME_BUILD_RT64`. Aucune ne dépend de SDL, d'ImGui ni de RT64 ; une seule
— `save_manager` — touche à `std::filesystem`.

## Décision

### 1. Le chemin moderne est conservé dans le même arbre, comme oracle

Des trois options du ticket, la deuxième est retenue : **conserver RT64 et SDL2
derrière l'interrupteur CMake existant**, dans le même arbre.

La justification n'est pas le confort, c'est [E09-S02](../stories/E09-qa/E09-S02-visual-comparison-harness.md).
Comparer le rendu Glide à une référence exige de rejouer **la même trace de
display list** dans les deux backends. Dans un seul arbre, c'est un binaire de
développement et deux appels ; sur deux branches, c'est deux binaires, deux
états de source à synchroniser, et une comparaison qu'on cesse de faire au bout
de trois semaines parce qu'elle coûte trop cher.

Supprimer le chemin moderne aurait simplifié le code — mais aurait laissé, comme
seule référence de justesse, un émulateur N64 tiers. Le projet perdrait ainsi sa
capacité à distinguer « Glide rend mal » de « le décodeur F3DDKR décode mal ».

**Le coût est accepté explicitement** : chaque changement de l'interface de rendu
devra être porté dans les deux backends. Ce coût reste faible tant que le
découpage de [E04-S01](../stories/E04-hle-f3ddkr/E04-S01-render-backend-interface.md)
tient — une interface qui reçoit des primitives déjà transformées.

**Jusqu'à quand.** Le chemin moderne est conservé au moins jusqu'à ce que le
rendu Glide soit validé par E09-S02 sur l'ensemble des scènes de référence. Après
quoi la question sera rouverte, et pas avant : c'est précisément pendant le
développement que l'oracle sert.

### 2. L'interrupteur de build

`DKR_RUNTIME_BUILD_RT64`, **défaut `OFF`**, propagé en `DKR_RUNTIME_HAS_RT64`.
Il est confirmé suffisant pour RT64 et ImGui, sur la foi du dépouillement
ci-dessus. La cible Windows 95 ne l'allume jamais.

Il **ne suffit pas encore pour SDL2**. Quatre références à faire passer sous
garde, dans le cadre de [E07-S03](../stories/E07-scope/E07-S03-sdl2-decoupling.md) :

| Emplacement | Nature |
|---|---|
| `runtime_input.cpp:571-572` | `SDL_GameController*` dans la signature publique de `input::poll` |
| `runtime_enhancements.cpp:10` | `#include <SDL.h>` inconditionnel |
| `runtime_enhancements.cpp:461,468` | `SDL_Window*`, `SDL_GetWindowSize` |

**Règle qui découle de cette ADR** : le socle — tout ce qui compile avec
`DKR_RUNTIME_BUILD_RT64=OFF` — ne référence ni RT64, ni ImGui, ni SDL2. Le
garde-fou de [E01-S04](../stories/E01-build/E01-S04-pe-import-guard-rail.md) le
vérifie à la sortie, sur la table d'imports du binaire produit.

### 3. Suivi de l'amont : gel, avec reprise sélective sur signalement

L'amont (`ThatGuyMcd/DKR-R`) reste une cible moderne 64 bits ; ce fork descend
vers une machine de 1995. Les deux trajectoires divergent, et fusionner
mécaniquement n'aurait pas de sens.

**Décision : gel de la base au commit `e5d1bbb`**, sans suivi automatique. Une
correction amont n'est reprise que si elle relève d'une des deux catégories
suivantes, et alors elle est reprise à la main, en `cherry-pick` documenté :

- **correction de justesse du jeu** — comportement indéfini, mauvais calcul,
  désynchronisation. Ces correctifs valent pour toutes les cibles, et sur une
  cible 32 bits sans SSE l'UB se manifeste différemment : les ignorer coûterait
  des heures de débogage attribuées à tort au portage ;
- **correction du pipeline de recompilation** — N64Recomp, politique de hooks,
  génération. Elle touche l'oracle lui-même.

Tout le reste — fonctionnalités modernes, interface, texture packs — est ignoré
par construction.

Aucun `remote` amont n'est configuré, délibérément : la reprise doit être un
geste conscient, pas un `git pull` machinal.

### 4. Limite entre patch et fork des dépendances

La règle de [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) est **confirmée sans
exception** : les worktrees `extern/rt64`, `extern/n64-modern-runtime`,
`extern/n64-modern-runtime/N64Recomp`, `RecompiledFuncs` et `RecompiledPatches`
ne sont jamais modifiés directement. Toute modification passe par
`patches/manifest.json`, avec sa somme de contrôle.

Le portage exigera des patchs lourds sur `ultramodern` et `librecomp`. La limite
au-delà de laquelle on forke plutôt que de patcher :

> Une dépendance est forkée quand un patch ne peut plus être exprimé comme une
> **modification locale et lisible** du fichier amont — c'est-à-dire quand il
> réécrit une unité de compilation entière, ou quand il devient impossible de
> décrire son intention en un paragraphe.

Concrètement, et sur la foi de E00-S01 :

- **`ultramodern` reste patché.** Six fichiers concernés, 12 `std::thread` et
  5 `std::mutex`. C'est dans l'épure.
- **`librecomp` reste patché** pour les constantes de RDRAM (ADR 0003 / E00-S06)
  et le remplacement de `std::filesystem`, qui sont des substitutions localisées.
- **Le premier candidat au fork est le système de mods de `librecomp`**, qui
  concentre les deux tiers des 101 usages de `std::filesystem` pour une
  fonctionnalité dont la cible Win95 n'a pas besoin. Si son neutralisation par
  patch dépasse la centaine de lignes, on forke.

Le patch `0002-portable-128-bit-multiply-for-32-bit-targets` illustre la bonne
forme : une branche `#elif` que la cible moderne ne compile jamais.

### 5. Tri des suites de tests : les 18 sont conservées

Aucune n'est supprimée. Le tri porte sur la **cible où chacune s'exécute**, ce
qui n'a de sens que parce que le point 1 conserve le chemin moderne.

**Socle — portables, exécutées sur les deux cibles (12)**

| Suite | Remarque |
|---|---|
| `dkr_save_codec` | logique pure, codec de sauvegarde |
| `save_manager` | **bloquée sur Win95** jusqu'à E01-S03 (`std::filesystem`) |
| `presentation_identity` | logique pure |
| `presentation_policy` | logique pure |
| `vi_presentation_policy` | logique pure — ses mentions de RT64 sont des commentaires |
| `audio_equalizer` | logique pure |
| `audio_mix_policy` | logique pure |
| `magic_code_policy` | logique pure |
| `quick_restart_policy` | logique pure |
| `intro_tail_policy` | logique pure |
| `character_select_animation_policy` | logique pure |
| `character_select_music_policy` | logique pure |

Ces douze sont **du gain net** : elles compilent pour i686 sans SSE et
constituent la première suite de tests exécutable sur la cible
([E09-S03](../stories/E09-qa/E09-S03-portable-test-suite.md)) sans rien écrire.

**Chemin moderne — conservées, exécutées sur l'hôte seulement (6)**

| Suite | Pourquoi elle ne s'applique pas à Win95 |
|---|---|
| `widescreen_policy` | le 21:9 n'a pas de sens en 640×480 |
| `modern_camera_policy` | caméra moderne, hors profil « Accurate » (E07-S01) |
| `interpolation_state_policy` | interpolation d'images propre à RT64 |
| `renderer_snapshot` | expose une image RDRAM **à RT64**, par construction |
| `motion_steering_policy` | pilotage gyroscopique, pas de manette gyro en 1995 |
| `rice_texture_pack_policy` | texture packs, hors périmètre (E07-S02) |

Elles restent valides et doivent continuer de passer : elles protègent l'oracle,
et un oracle cassé ne vaut rien.

## Conséquences

- **E07-S01** (profil « Accurate » seul) ne supprime pas les politiques modernes,
  il les exclut de la cible Win95. La formulation du ticket doit être ajustée.
- **E09-S03** (suite de tests portable) hérite de douze suites déjà écrites.
- **E09-S02** devient réalisable telle que conçue : un arbre, deux backends, une
  trace.
- Le coût de maintenance des deux backends est accepté, borné dans le temps par
  la validation de E09-S02.
- Le risque assumé : si le découpage de E04-S01 se dégrade, le coût des deux
  backends croît sans qu'on s'en aperçoive. C'est à surveiller au moment où
  l'interface de rendu changera.

## Références

- [`docs/research/win95-blockers.md`](../research/win95-blockers.md) — le dépouillement des gardes
- [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) — frontières protégées
- `runtime-recomp/CMakeLists.txt:37` — `DKR_RUNTIME_BUILD_RT64`
- `runtime-recomp/CMakeLists.txt:178,291` — propagation en `DKR_RUNTIME_HAS_RT64`
- `../../Diddy-Kong-Racing/docs/adr/0003-strategie-fork.md` — même arbitrage côté portage natif
