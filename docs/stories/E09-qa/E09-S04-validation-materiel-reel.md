# E09-S04 — Validation sur matériel réel

| | |
|---|---|
| **Épic** | E09 — Intégration, QA et distribution |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E05-S07, E06-S03, E08-S04, E09-S02 |
| **Bloque** | E09-S05 |

## Contexte

L'émulation Voodoo de E09-S01 est fidèle sur le plan fonctionnel et trompeuse sur
le plan des performances : l'hôte moderne exécute le rendu bien plus vite que le
matériel d'époque, et rien dans l'émulateur ne reproduit la bande passante du bus
PCI, la latence du disque, ou le comportement thermique d'une machine réelle.

Tout ce projet est construit sur une contrainte de budget. Elle ne peut être
validée que sur la machine visée.

Ce ticket doit avoir lieu **plusieurs fois**, pas une seule à la fin. Un premier
passage dès que le jeu affiche une image sur du matériel réel vaut mieux qu'une
validation exhaustive trois mois plus tard : il révèle les surprises quand il est
encore temps d'y répondre.

## Objectif

Valider le portage sur du matériel authentique, et confirmer que le plancher
matériel annoncé est le bon.

## Périmètre

**Dans :** l'exécution sur matériel réel, la mesure, et l'ajustement du plancher.

**Hors :** la correction des défauts trouvés, qui remonte aux tickets concernés.

## Travail

1. Réunir la configuration de validation nommée par l'ADR de E00-S05 : processeur,
   mémoire, carte 3dfx, carte son, Windows 95 OSR2.5.
2. Réunir si possible une seconde configuration plus modeste — proche du plancher —
   et une plus véloce, pour situer la plage de fonctionnement.
3. Installer et lancer. Relever tout ce qui diffère de l'émulateur, en particulier
   au démarrage et à l'initialisation de Glide.
4. Mesurer avec l'instrumentation de E08-S01, sur une session de jeu réelle :
   temps par image en distribution, ventilation par poste, occupation mémoire,
   défauts de page, sous-alimentations audio.
5. Comparer poste par poste aux mesures obtenues sous émulation, et documenter les
   écarts. Ce tableau d'écarts a une valeur durable : il indique dans quelle mesure
   on peut faire confiance à l'émulateur pour la suite.
6. Vérifier ce que l'émulateur ne peut pas montrer : compatibilité des pilotes
   3dfx réels, comportement de la carte son, manettes du commerce, sortie sur un
   moniteur cathodique, temps de chargement depuis un disque d'époque.
7. Jouer réellement. Une partie complète, plusieurs niveaux, en multijoueur. Les
   défauts de jouabilité — latence d'entrée, irrégularité de cadence, imprécision
   de conduite — ne se révèlent pas autrement.
8. Confirmer ou corriger le plancher matériel de l'ADR de E00-S05 sur la foi de
   ces mesures.

## Critères d'acceptation

- [ ] Le jeu tourne sur au moins une configuration matérielle authentique.
- [ ] Les mesures de E08-S01 sont relevées sur matériel réel.
- [ ] Le tableau des écarts émulateur / matériel réel est documenté, poste par
      poste.
- [ ] Les pilotes 3dfx réels, la carte son et les manettes du commerce sont
      vérifiés.
- [ ] Une partie complète est jouée, multijoueur compris.
- [ ] Le plancher matériel de l'ADR est confirmé ou corrigé.
- [ ] Les défauts trouvés sont consignés et attribués à un ticket.

## Risques

C'est ici que les hypothèses du projet sont réellement mises à l'épreuve. Un écart
important entre l'émulation et le matériel réel peut remettre en cause le plancher
matériel, voire la faisabilité sur la classe de machine visée. D'où l'insistance
sur des passages **précoces et répétés** : découvrir cet écart tôt laisse le temps
d'y répondre, le découvrir à la fin ne laisse que le choix de revoir l'annonce.

## Références

- E00-S05 — plancher matériel à confirmer
- E08-S01 — instrumentation
- E09-S01 — environnement émulé et ses limites documentées
