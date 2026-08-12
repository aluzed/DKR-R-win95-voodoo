# E06-S02 — Entrées : clavier et manette

| | |
|---|---|
| **Épic** | E06 — Plateforme Windows 95 |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E06-S01 |
| **Bloque** | E09-S04 |

## Contexte

`runtime_input.cpp` (25 Ko) gère aujourd'hui clavier, manettes et gyroscope via
SDL2, avec remappage complet. La logique de correspondance vers la manette N64 est
du code du projet, portable, et elle doit être conservée ; c'est la couche
d'acquisition qui change.

Sous Windows 95, deux voies :

- **DirectInput**, à partir de DirectX 3, qui gère les manettes et les joysticks
  de façon uniforme ;
- **l'API joystick de `winmm`** (`joyGetPosEx`), plus ancienne, plus simple,
  disponible partout, mais limitée en nombre d'axes et de boutons.

DirectInput est la bonne voie, avec `winmm` en repli si le pilote de la manette
n'expose pas DirectInput.

Deux différences d'époque à intégrer : les manettes de 1998 sont analogiques mais
souvent mal calibrées, et le panneau de configuration de Windows expose une
calibration système dont il faut tenir compte. Et il n'y a pas de vibration —
DKR n'utilise pas le Rumble Pak, donc c'est sans conséquence, mais à vérifier.

## Objectif

Livrer l'acquisition des entrées sous Windows 95, branchée sur la logique de
correspondance existante.

## Périmètre

**Dans :** acquisition clavier et manette, calibration, remappage, correspondance
vers la manette N64.

**Hors :** le gyroscope, qui n'a pas de sens sur cette cible et disparaît avec
E07-S02.

## Travail

1. Isoler dans `runtime_input.cpp` ce qui relève de SDL2 et ce qui relève de la
   logique de correspondance. Cette dernière est conservée telle quelle : elle est
   testée et elle n'a aucune raison de changer.
2. Implémenter l'acquisition clavier. En plein écran accéléré, décider entre les
   messages de fenêtre et l'acquisition directe DirectInput : la seconde évite la
   répétition automatique et la latence de la file de messages, ce qui compte pour
   un jeu de course.
3. Implémenter l'acquisition manette par DirectInput, avec repli sur `joyGetPosEx`.
4. Traiter la calibration et la zone morte. Les manettes analogiques de l'époque
   dérivent ; une zone morte configurable est nécessaire, pas optionnelle.
5. Traiter la correspondance des axes vers le stick analogique de la N64, en
   respectant la plage et la forme de réponse attendues par le jeu. Une plage mal
   calibrée rend la conduite imprécise sans qu'aucune erreur ne soit visible.
6. Traiter le multijoueur : jusqu'à quatre manettes, la N64 en acceptant quatre.
   Vérifier ce que le matériel de l'époque permet réellement — deux ports de jeu
   sont plus courants que quatre.
7. Conserver le remappage, avec sa persistance dans le fichier de configuration
   (E06-S05) plutôt que dans l'interface ImGui supprimée.
8. Mesurer la latence d'entrée et la comparer à celle de la cible moderne. Sur un
   jeu de course, la latence est une caractéristique de jouabilité, pas un détail.

## Critères d'acceptation

- [ ] Le clavier fonctionne en plein écran, sans latence de file de messages.
- [ ] Une manette DirectInput fonctionne, avec repli `winmm` vérifié.
- [ ] La zone morte et la calibration sont configurables.
- [ ] La correspondance vers le stick N64 respecte plage et forme de réponse,
      vérifiée par comparaison à la cible moderne.
- [ ] Le nombre de manettes réellement supporté est déterminé et documenté.
- [ ] Le remappage est conservé et persistant.
- [ ] La latence d'entrée est mesurée et comparée à la référence.
- [ ] La logique de correspondance existante est réutilisée, non réécrite.

## Risques

Une correspondance d'axe imprécise ne se voit pas : elle se ressent, sous forme
d'une conduite qui « ne répond pas pareil ». C'est un défaut difficile à
diagnostiquer après coup, d'où la comparaison objective de l'étape 8 plutôt qu'une
appréciation au jeu.

## Références

- `runtime-recomp/src/game/runtime_input.{hpp,cpp}` — 25 Ko, logique à conserver
- `runtime-recomp/src/game/motion_steering_policy.hpp` — gyroscope, hors périmètre
- E06-S05 — persistance de la configuration
