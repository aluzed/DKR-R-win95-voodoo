# E07-S03 — Découplage d'avec SDL2

| | |
|---|---|
| **Épic** | E07 — Réduction de périmètre |
| **Statut** | TODO |
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

- [ ] Chaque usage de SDL2 est classé plateforme ou logique.
- [ ] L'interface de plateforme est minimale et couvre les deux implémentations.
- [ ] La logique de correspondance des entrées et la politique audio restent
      uniques, non dupliquées.
- [ ] La sélection se fait à la compilation.
- [ ] La cible moderne est inchangée, vérifié par les tests et par comparaison de
      comportement.
- [ ] Aucun symbole SDL2 dans le binaire Win95.
- [ ] Les suites de tests conservées passent sur les deux cibles.

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
