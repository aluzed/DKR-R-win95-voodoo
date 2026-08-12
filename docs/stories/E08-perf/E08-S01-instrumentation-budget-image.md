# E08-S01 — Instrumentation et budget par image

| | |
|---|---|
| **Épic** | E08 — Performance |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E02-S03, E05-S01 |
| **Bloque** | E08-S02, E08-S03, E08-S04, E09-S03 |

## Contexte

Tout ce projet tient dans une contrainte : 33,3 ms par image, sur un processeur
qui n'a pas de marge. Chaque ticket précédent a demandé une mesure et l'a inscrite
« au budget ». Ce ticket construit le budget lui-même.

Les postes attendus, chacun mesuré par son ticket d'origine :

| Poste | Ticket d'origine |
|---|---|
| CPU du jeu recompilé | E00-S03, E02-S06 |
| Microcode audio | E03-S02 |
| Décodage de la display list | E04-S02 |
| Transformation des sommets | E04-S03 |
| Découpage | E04-S05 |
| Décodage de texture | E04-S07 |
| Backend Glide, côté CPU | E05-S01 à E05-S08 |
| Téléchargement de texture | E05-S02 |
| Sortie audio | E06-S03 |
| Boucle de messages | E06-S01 |
| Attente de présentation | E06-S04 |

Sur une machine sans profileur moderne, l'instrumentation doit être intégrée au
programme. Et elle doit être bon marché : un instrument qui coûte 5 % du budget
fausse ce qu'il mesure.

## Objectif

Livrer une instrumentation permanente qui ventile le temps par image entre ses
postes, lisible sur la machine cible.

## Périmètre

**Dans :** l'instrumentation, l'affichage, l'export, et l'établissement du budget.

**Hors :** les optimisations elles-mêmes (E08-S02, E08-S03, E08-S04).

## Travail

1. Implémenter des compteurs par zone, sur une source de temps de coût mesuré
   (E02-S03). Sur Pentium, `RDTSC` est le bon instrument : quelques cycles par
   lecture. Calibrer sa fréquence au démarrage.
2. Poser les zones aux frontières des postes ci-dessus, en visant une dizaine de
   zones — assez pour attribuer, assez peu pour ne pas coûter.
3. Mesurer le coût de l'instrumentation elle-même, et la rendre désactivable par
   la configuration (E06-S05). Consigner ce coût.
4. Afficher les compteurs à l'écran, par les rectangles 2D de E05-S07 : temps par
   image, ventilation par poste, nombre de triangles, nombre de changements
   d'état, occupation de mémoire de texture, sous-alimentations audio.
5. Rapporter les distributions, pas les moyennes : médiane et 99ᵉ centile par
   poste. C'est le centile haut qui produit les saccades et les coupures audio.
6. Implémenter l'export vers un fichier, pour analyser hors ligne une session
   jouée sur la machine cible. C'est le seul moyen d'étudier finement ce qui s'y
   passe.
7. Établir le budget dans `docs/research/frame-budget.md` : allocation cible par
   poste, mesure réelle, écart. Ce document est le tableau de bord de tout E08.
8. Identifier les trois postes les plus coûteux et les orienter vers E08-S02,
   E08-S03 ou E08-S04 selon leur nature.

## Critères d'acceptation

- [ ] Les compteurs couvrent tous les postes du tableau.
- [ ] Le coût de l'instrumentation est mesuré et elle est désactivable.
- [ ] L'affichage à l'écran est lisible sur la machine cible.
- [ ] Médiane et 99ᵉ centile sont rapportés par poste.
- [ ] L'export vers fichier permet une analyse hors ligne.
- [ ] `docs/research/frame-budget.md` donne allocation, mesure et écart par poste.
- [ ] Les trois postes les plus coûteux sont identifiés et orientés.

## Risques

Sans ce ticket, l'optimisation se fait à l'intuition — et l'intuition sur du
matériel de 1998, avec des caches minuscules et une hiérarchie mémoire très
différente de celle d'aujourd'hui, se trompe régulièrement. Il doit être livré
**avant** toute optimisation, sans quoi E08-S02 et E08-S03 travaillent à l'aveugle.

## Références

- `runtime-recomp/src/game/runtime_telemetry.cpp` — collecte conservée par E07-S02
- E00-S03 — première estimation du budget
- E06-S03 — les sous-alimentations audio comme indicateur d'ensemble
