# E06-S04 — Cadence d'affichage et synchronisation

| | |
|---|---|
| **Épic** | E06 — Plateforme Windows 95 |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E02-S03, E05-S01 |
| **Bloque** | E08-S01 |

## Contexte

DKR tourne à 30 images par seconde sur la N64, cadence à laquelle sa simulation
est calibrée. `vi_presentation_policy.hpp` implémente aujourd'hui cette politique
de présentation, et le mode Moderne y ajoute une interpolation vers des taux de
rafraîchissement élevés — fonctionnalité qui disparaît avec le profil « Accurate »
seul (E07-S01).

Ce qui reste est plus simple mais pas trivial : présenter une image toutes les
33,3 ms, sur une carte 3dfx dont l'échange de tampons se synchronise sur le
balayage du moniteur.

Le nœud est là. Un moniteur de 1998 en 640 × 480 balaie typiquement à 60, 72 ou
85 Hz. À 60 Hz, une image de jeu occupe exactement deux balayages — cas idéal. À
72 ou 85 Hz, le rapport n'est plus entier, et la présentation synchronisée
produira une saccade régulière. Et si le jeu rate son échéance, l'échange
synchronisé attend le balayage suivant, ce qui fait perdre une image entière : à
30 images par seconde, en perdre une se voit beaucoup.

## Objectif

Présenter le jeu à sa cadence d'origine, régulièrement, sur les modes d'affichage
disponibles sur la cible.

## Périmètre

**Dans :** la cadence de présentation, la synchronisation, la mesure de régularité.

**Hors :** l'interpolation vers des taux élevés, supprimée avec le mode Moderne.

## Travail

1. Relever les fréquences de rafraîchissement réellement disponibles à la
   résolution retenue, sur la cible.
2. Trancher entre échange synchronisé et échange immédiat. Le synchronisé évite le
   déchirement ; l'immédiat évite de perdre une image entière quand l'échéance est
   ratée. Sur une machine au budget serré, le second peut être le meilleur choix —
   à décider sur mesure, et à rendre configurable (E06-S05).
3. Traiter les fréquences non multiples de 30 Hz : présenter au balayage le plus
   proche, en mesurant la saccade induite. Documenter le mode d'affichage
   recommandé.
4. Implémenter la régulation de cadence sur l'horloge de E02-S03, en découplant la
   cadence de simulation de la cadence de présentation. La simulation doit avancer
   à 30 Hz quoi qu'il arrive, sans quoi le jeu tourne au ralenti ou en accéléré.
5. Traiter le cas du budget dépassé : quand une image prend plus de 33,3 ms, décider
   entre sauter une présentation et laisser la simulation prendre du retard. Le
   comportement de la console est la référence.
6. Mesurer la régularité : distribution des temps entre présentations, pas
   seulement la moyenne. Une moyenne de 30 images par seconde avec une distribution
   irrégulière donne un jeu qui se sent mauvais malgré un chiffre correct.
7. Vérifier la cohérence avec la synchronisation audio (E06-S03) : les deux
   horloges doivent rester d'accord sur la durée.

## Critères d'acceptation

- [ ] Les fréquences disponibles à la résolution retenue sont relevées.
- [ ] Le choix synchronisé / immédiat est justifié par une mesure, et configurable.
- [ ] La cadence de simulation reste à 30 Hz indépendamment de la présentation.
- [ ] Le comportement en cas de dépassement de budget est décidé et documenté.
- [ ] La régularité est mesurée en distribution, pas en moyenne.
- [ ] Le mode d'affichage recommandé est documenté.
- [ ] Aucune dérive entre horloge audio et horloge vidéo sur vingt minutes.

## Risques

Une cadence irrégulière est perçue comme une mauvaise performance même quand le
nombre moyen d'images par seconde est correct. C'est particulièrement vrai à
30 Hz, où chaque image compte double. La mesure en distribution de l'étape 6 est
ce qui permet de distinguer « lent » de « irrégulier » — deux problèmes dont les
remèdes n'ont rien à voir.

## Références

- `runtime-recomp/src/game/vi_presentation_policy.hpp`
- `runtime-recomp/src/game/interpolation_state_policy.hpp` — supprimé avec E07-S01
- E02-S03 — base de temps
