# E01-S01 — Fichier de toolchain CMake : i686, sans SSE

| | |
|---|---|
| **Épic** | E01 — Chaîne de build 32 bits Windows 95 |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E00-S02 |
| **Bloque** | E01-S02, E01-S03, E01-S05, E02-S01 |

## Contexte

`runtime-recomp/CMakeLists.txt` suppose un hôte moderne : CMake 3.24, C17 et
C++20 exigés, MSVC ou Clang/GCC, RT64 et SDL2. La cible Win95 est un autre monde,
et elle doit cohabiter avec l'existant sans le casser — l'oracle de comparaison
en dépend (E00-S07).

Le point le plus facile à rater est le jeu d'instructions. Un compilateur moderne
en 32 bits émet du SSE2 par défaut pour l'arithmétique flottante ; le Pentium II
n'a que MMX et x87. L'échec ne se voit pas à la compilation : il se voit au
lancement, par une exception d'instruction invalide, éventuellement des mois plus
tard dans une fonction rarement atteinte.

## Objectif

Ajouter une cible de build `win95` qui produit un binaire 32 bits dont il est
**prouvé** qu'aucune instruction n'excède le Pentium II, sans altérer les cibles
Windows, Linux et macOS existantes.

## Périmètre

**Dans :** le fichier de toolchain, l'intégration CMake, la vérification du jeu
d'instructions, un script de build.

**Hors :** faire compiler réellement les sources du jeu — elles ne le sont pas
encore (E01-S02, E01-S03, E01-S05).

## Travail

1. Écrire `cmake/toolchain-win95.cmake` avec le compilateur retenu par l'ADR de
   E00-S02, `CMAKE_SYSTEM_NAME Windows`, `CMAKE_SYSTEM_PROCESSOR i686`, et le jeu
   d'options : `-m32 -march=pentium2 -mtune=pentium3 -mfpmath=387 -mno-sse
   -mno-sse2` (à transposer selon la toolchain).
2. Définir `_WIN32_WINNT`, `WINVER` et `NTDDI_VERSION` à la valeur Windows 95 la
   plus élevée acceptée par les en-têtes, de sorte que l'usage d'une API trop
   récente échoue **à la compilation** plutôt qu'au lancement.
3. Introduire une option `DKR_RUNTIME_TARGET_WIN95` dans
   `runtime-recomp/CMakeLists.txt` qui force `DKR_RUNTIME_BUILD_RT64=OFF`,
   court-circuite SDL2 et ImGui, et sélectionne les sources de la cible.
4. Écrire le vérificateur de jeu d'instructions : un script qui désassemble tous
   les objets produits et échoue si une instruction hors Pentium II apparaît.
   Il doit couvrir le code de démarrage du CRT et la bibliothèque standard, pas
   seulement les sources du projet — c'est là que le SSE se glisse.
5. Brancher ce vérificateur comme étape post-build obligatoire de la cible, pas
   comme outil facultatif.
6. Écrire `Build-Win95.sh` sur le modèle de `Build-Linux.sh`, avec les mêmes
   vérifications de prérequis en début de script.
7. Vérifier que `Build-Linux.sh` et `Build-Windows.cmd` produisent des binaires
   inchangés — comparaison de la sortie de configuration CMake avant et après.

## Critères d'acceptation

- [ ] `cmake -S runtime-recomp --toolchain cmake/toolchain-win95.cmake` configure
      sans erreur.
- [ ] Un fichier témoin minimal compile et se lie en PE 32 bits.
- [ ] Le vérificateur de jeu d'instructions s'exécute automatiquement après le
      lien et échoue sur un objet contenant volontairement du SSE — testé par
      injection, pas supposé.
- [ ] Le vérificateur couvre le code du CRT et de la bibliothèque standard.
- [ ] L'usage d'une API postérieure à Windows 95 échoue à la compilation.
- [ ] Les cibles Windows, Linux et macOS existantes sont inchangées, preuve par
      comparaison de la configuration CMake.
- [ ] `Build-Win95.sh` existe et signale clairement ses prérequis manquants.

## Risques

`-mfpmath=387` change les résultats en virgule flottante : le x87 calcule en
80 bits en interne et arrondit à la sortie, là où le SSE calcule en 32 ou 64 bits
strictement. Le code du jeu recompilé exécute l'arithmétique flottante du VR4300,
qui est en IEEE 754 simple et double précision. Des écarts d'arrondi sont donc
attendus, et ils peuvent modifier la physique. À surveiller dès E01-S05, et à
mesurer contre l'oracle en E09-S02 : c'est un candidat probable pour la première
divergence de comportement observée.

## Références

- `runtime-recomp/CMakeLists.txt:26-31, 36-37`
- `Build-Linux.sh` — modèle de script de build avec vérification de prérequis
- ADR de E00-S02 — toolchain retenue
