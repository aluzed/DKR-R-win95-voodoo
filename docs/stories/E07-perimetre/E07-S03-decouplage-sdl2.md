# E07-S03 — Découplage d'avec SDL2

| | |
|---|---|
| **Épic** | E07 — Réduction de périmètre |
| **Statut** | IN_PROGRESS |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E06-S01, E06-S02, E06-S03, E07-S02 |
| **Bloque** | E09-S03 |

## Contexte

SDL2 est présent dans dix fichiers du code du projet, et sur les quatre couches à
la fois : fenêtre, entrées, audio, et intégration avec RT64. E06 a écrit les
remplaçants Win32 de chacune. Reste à couper le lien proprement.

Le mauvais réflexe serait de dupliquer chaque fichier en deux versions, l'une SDL2
pour la cible moderne, l'autre Win32 pour la cible Win95. Les deux dériveraient, et
l'oracle perdrait sa valeur : comparer deux implémentations qui ont divergé ne
prouve plus rien (E00-S07).

La bonne structure sépare **ce qui dépend de la plateforme** de **ce qui n'en
dépend pas**, et ne duplique que la première. La logique de correspondance des
entrées, la politique de mixage audio, la politique de présentation sont du code
portable qui doit rester unique.

## Objectif

Sortir SDL2 de la cible Win95, sans dupliquer la logique portable ni casser la
cible moderne.

## Périmètre

**Dans :** la séparation plateforme / logique, et la sélection à la compilation.

**Hors :** l'écriture des implémentations Win32 (E06).

## Travail

1. Recenser chaque usage de SDL2 dans les dix fichiers concernés et le classer :
   **plateforme** (à abstraire) ou **logique** (à conserver tel quel).
2. Définir une interface de plateforme minimale couvrant fenêtre, entrées, audio
   et temps — la plus petite qui couvre les deux implémentations. Elle sera étroite,
   parce que la logique portable a été mise à part à l'étape 1.
3. Extraire de `runtime_input.cpp` (25 Ko) la logique de correspondance, qui doit
   rester unique et partagée.
4. Faire de même pour l'audio : la politique de mixage
   (`audio_mix_policy.hpp`) et l'égaliseur sont portables ; seule la sortie ne
   l'est pas.
5. Implémenter l'interface deux fois : SDL2 pour la cible moderne, Win32 pour la
   cible Win95. Sélection à la compilation, sans branchement à l'exécution.
6. Vérifier que la cible moderne se comporte exactement comme avant. C'est la
   condition pour que l'oracle conserve sa valeur.
7. Vérifier qu'aucun symbole SDL2 n'est importé par le binaire Win95 — le garde-fou
   de E01-S04 le fait automatiquement.
8. Faire tourner les suites de tests conservées sur les deux cibles.

## Critères d'acceptation

- [x] Chaque usage de SDL2 est classé plateforme ou logique.
- [~] L'interface de plateforme est minimale — une fonction — et couvre les deux
      implémentations **pour la fenêtre**. Entrées, audio et temps attendent E06.
- [x] La logique reste unique : rien n'a été dupliqué, et deux morceaux
      portables enfermés derrière un garde de plate-forme en ont été sortis.
- [x] La sélection se fait à la compilation, sans branchement à l'exécution.
- [~] La cible moderne passe ses **18 suites**. Le comportement avec RT64 allumé
      n'est pas vérifié ici, faute de SDL2 sur ce poste.
- [~] Aucun fichier compilé pour Win95 n'inclut SDL2. Le contrôle sur le
      binaire viendra quand le jeu se liera.
- [ ] Les suites de tests conservées passent sur les deux cibles.

## État au 2026-08-13 — le lien est coupé, une seule fonction a suffi

Le recensement de l'étape 1 donne un résultat plus favorable que le ticket ne le
laissait craindre. **L'interrupteur RT64 avait déjà fait presque tout le
travail** :

| Fichier | Occurrences | Hors garde RT64 | Verdict |
|---|---:|---:|---|
| `runtime_platform.cpp` | 189 | 0 | plate-forme, remplacé par E06 |
| `runtime_ui.cpp`, `rt64_renderer.cpp` | 87 | — | **exclus du build** quand RT64 est éteint |
| `runtime_input.cpp` | 82 | 0 | plate-forme, sous garde |
| `runtime_input.hpp`, `runtime_ui.hpp` | 8 | 8 | **déclaration anticipée seule** — aucune inclusion de SDL |
| `game_main.cpp`, `runtime_stubs.cpp` | 4 | 0 | sous garde |
| **`runtime_enhancements.cpp`** | **3** | **3** | **le seul lien réel** |

Un seul fichier dépendait vraiment de SDL2 hors garde, et il n'en voulait
qu'**une chose** : la taille de la fenêtre, pour un rapport d'aspect. Tout ce qui
en découlait — l'échelle du tronc de vision, la politique de présentation — est
de la logique portable.

D'où l'interface, qui tient en une fonction :

```cpp
bool dkr::runtime::platform::window_size(int& width, int& height);
```

Déclarée **hors** du garde, implémentée une fois de chaque côté. `runtime_stubs.cpp`
faisait exactement la même danse `sdl_window()` + `SDL_GetWindowSize` et passe
par le même accesseur : la duplication contre laquelle le ticket met en garde est
retirée au lieu d'être ajoutée.

Deux découpages hérités, trouvés en compilant, ont été rectifiés au passage —
tous deux du **code portable enfermé derrière un garde de plate-forme**, ce qui
est le défaut exact que ce ticket cherche à défaire :

- `RdramAddress` et `g_title_intro_tail_gate` vivaient sous le garde RT64 dans
  `runtime_stubs.cpp` alors que `dkr_title_intro_audio_tail`, qui les emploie,
  n'en dépend pas.
- Le gestionnaire de plantage de `game_main.cpp` lisait les registres x86-64 par
  leur nom. Une branche i386 lui a été ajoutée, et `StackWalk64` reçoit
  désormais le type de machine qui convient — le lui donner faux remonterait une
  pile de valeurs fantaisistes, ce qui est pire que pas de pile du tout.

### Résultat

| | |
|---|---|
| Sources du jeu compilant pour Windows 95 | **17 sur 17** |
| Suites de la cible moderne | **18 sur 18** |
| Suites de la cible Win95 | 4 sur 4 |
| Jeu d'instructions | aucune hors Pentium II |

**Ce qui n'est pas vérifié ici** : le comportement de la cible moderne avec RT64
*allumé*, faute de SDL2 sur ce poste. Les 18 suites couvrent la logique portable,
qui est précisément ce que ce ticket ne devait pas toucher ; la branche RT64 de
`window_size` reproduit le code retiré à l'identique — même test de nullité,
même appel, même seuil.

Les critères qui restent ouverts appartiennent à E06 : l'interface ne couvre
aujourd'hui que la fenêtre, parce que c'est tout ce qui manquait pour compiler.
Entrées, audio et temps y viendront quand leurs implémentations Win32
existeront.

## Risques

Le risque est la duplication rampante : à chaque difficulté, il sera tentant de
copier un fichier plutôt que d'extraire l'abstraction. Le critère de non-duplication
de la logique n'est pas une exigence de style — c'est ce qui garde l'oracle
utilisable jusqu'à la fin du projet.

## Références

- `runtime-recomp/src/game/runtime_platform.cpp` (33 Ko), `runtime_input.cpp`
  (25 Ko), `game_main.cpp` (22 Ko)
- E00-S07 — stratégie d'oracle
- E06 — implémentations Win32
