# E02-S06 — Amorçage : du point d'entrée Win32 au premier appel du jeu

| | |
|---|---|
| **Épic** | E02 — Substrat système Windows 95 |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E01-S05, E02-S02, E02-S04 |
| **Bloque** | E04-S08, E09-S02 |

## Contexte

C'est le jalon qui prouve que E01 et E02 tiennent debout : le code recompilé du
jeu s'exécute réellement sous Windows 95. Sans rendu, sans audio, sans entrées —
mais il s'exécute, et il soumet des tâches graphiques.

`game_main.cpp` (22 Ko) et `game_registration.cpp` orchestrent aujourd'hui cet
amorçage autour de SDL2 : création de la fenêtre, sélection de ROM par le
lanceur, initialisation du renderer, puis remise du contrôle à `librecomp`. Il
faut le même enchaînement, sans SDL2 et sans RT64.

Le renderer de diagnostic (`null_renderer.cpp`) est exactement l'outil de ce
jalon : il implémente `RendererContext`, compte les display lists et les
présentations, et n'affiche rien. Il permet de valider tout le chemin CPU avant
d'écrire une seule ligne de Glide.

## Objectif

Faire démarrer le jeu recompilé sous Windows 95 jusqu'à la soumission régulière
de tâches graphiques, avec le renderer de diagnostic.

## Périmètre

**Dans :** l'amorçage, l'enregistrement du jeu, le branchement du renderer de
diagnostic, le journal de démarrage.

**Hors :** fenêtre (E06-S01), entrées (E06-S02), audio (E06-S03), rendu (E04, E05).

## Travail

1. Écrire `platform/win95/main.cpp` : point d'entrée, initialisation de la couche
   de compatibilité (E01-S03), de l'horloge (E02-S03), du journal.
2. Reprendre de `game_main.cpp` la séquence d'enregistrement du jeu auprès de
   `librecomp` et de démarrage, en retirant SDL2. Découper plutôt que dupliquer :
   la logique d'enregistrement doit rester partagée avec la cible moderne, sans
   quoi les deux dérivent.
3. Résoudre la ROM sans lanceur : argument de ligne de commande, ou chemin lu dans
   la configuration, ou fichier de nom convenu dans le dossier de l'application.
   E06-S06 traitera l'ergonomie ; ici, le plus simple suffit.
4. Instancier `DiagnosticRenderer` comme contexte de rendu. Vérifier qu'il ne
   dépend ni de SDL2 ni de RT64 — son en-tête n'inclut qu'`ultramodern`, ce qui
   est de bon augure, mais son fichier source est à vérifier.
5. Faire tourner la boucle. Le compteur de display lists de `DiagnosticRenderer`
   doit progresser régulièrement : c'est le signe que le fil de jeu vit, que
   l'ordonnanceur commute, et que le jeu produit des images.
6. Écrire un journal de démarrage détaillé dans un fichier : chaque étape franchie,
   chaque fil créé, chaque tâche soumise. C'est le seul outil de diagnostic
   disponible sur la machine cible.
7. Mesurer, avec les compteurs de `DiagnosticRenderer`, la cadence de soumission
   des tâches graphiques, et la comparer à l'attendu de 30 par seconde. C'est la
   première mesure de performance réelle du projet sur la cible, et elle confronte
   directement l'extrapolation de E00-S03 aux faits.

## Critères d'acceptation

- [ ] Le jeu recompilé démarre sous Windows 95 émulé.
- [ ] Le compteur de display lists de `DiagnosticRenderer` progresse régulièrement.
- [ ] La cadence de soumission est mesurée et comparée à la prévision de E00-S03.
- [ ] Le journal de démarrage trace chaque étape et est lisible depuis la machine
      cible.
- [ ] Aucune dépendance à SDL2, ImGui ou RT64 dans le binaire produit — vérifié
      par la table d'imports (E01-S04).
- [ ] La logique d'enregistrement du jeu reste partagée avec la cible moderne.
- [ ] Le jeu atteint au moins l'écran-titre du point de vue du CPU, c'est-à-dire
      soumet les tâches graphiques correspondantes.

## Risques

L'écart entre la cadence mesurée ici et la prévision de E00-S03 est l'information
la plus importante du projet à ce stade. S'il est mauvais, il vaut mieux le
découvrir maintenant, avant les deux épics les plus lourds (E04 et E05), et
rouvrir l'ADR de plancher matériel.

## Références

- `runtime-recomp/src/game/game_main.cpp`, `game_registration.cpp`
- `runtime-recomp/src/game/null_renderer.{hpp,cpp}`
- `docs/ARCHITECTURE.md` — chemin d'exécution complet
