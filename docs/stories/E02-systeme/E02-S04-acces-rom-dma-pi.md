# E02-S04 — Accès à la ROM et DMA du bus périphérique

| | |
|---|---|
| **Épic** | E02 — Substrat système Windows 95 |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E00-S06, E02-S01 |
| **Bloque** | E02-S06 |

## Contexte

Le jeu lit ses données par DMA depuis la cartouche. `librecomp` traduit ces
transferts en accès à l'image de la ROM, qu'il charge vraisemblablement
entièrement en mémoire — 12 Mo pour DKR, ce qui est indolore sur une machine
moderne et représente près de 20 % du budget d'une machine à 64 Mo.

Deux autres points changent de nature sur la cible :

- **La validation de la ROM.** Le calcul du SHA-1 des 12 Mo, instantané
  aujourd'hui, prend un temps notable sur un Pentium II. À mesurer, et à rendre
  visible à l'utilisateur si nécessaire.
- **La provenance.** Le lanceur SDL actuel présente un sélecteur de fichier
  graphique ; il disparaît avec SDL2 (E06-S06).

Le portage natif voisin a rencontré exactement ce problème et l'a résolu par un
espace d'adressage ROM au-dessus de fichiers, lu par positionnement et lecture
plutôt que chargé en mémoire — approche directement transposable.

## Objectif

Rendre l'accès à la ROM conforme au budget mémoire de E00-S06, sans changer la
sémantique des transferts vue par le jeu.

## Périmètre

**Dans :** le chargement de la ROM, sa validation, et le chemin de DMA.

**Hors :** la sélection du fichier par l'utilisateur (E06-S06) et les sauvegardes
(E02-S05).

## Travail

1. Relever comment `librecomp` charge la ROM et sert les transferts DMA, et
   mesurer l'empreinte mémoire réelle.
2. Appliquer la décision de E00-S06 : image complète en mémoire, ou lecture par
   morceaux. Si c'est la lecture par morceaux, implémenter un cache de blocs
   dimensionné sur les motifs d'accès observés — la taille du cache se choisit
   sur une mesure, pas sur une intuition.
3. Mesurer le coût des transferts. Le jeu charge des données pendant les
   transitions d'écran ; un accès disque de 1998 est lent, et un chargement de
   niveau qui prend dix secondes est une régression visible même si le rendu est
   parfait.
4. Mesurer le temps de validation SHA-1 sur la cible. S'il dépasse une poignée de
   secondes, prévoir un indicateur de progression, ou une mise en cache du
   résultat indexée par chemin, taille et date — jamais un contournement de la
   validation elle-même.
5. Traiter l'ordre des octets. La ROM peut être fournie en `.z64`, `.n64` ou
   `.v64` ; la normalisation existe déjà dans le runtime (le README mentionne un
   SHA-1 « après normalisation de l'ordre des octets ») — vérifier qu'elle ne
   suppose pas de disposer de l'image entière en mémoire.
6. Traiter les chemins de fichiers Windows 9x : noms courts, chemins en page de
   code, absence d'API Unicode fonctionnelle (E01-S03).
7. Vérifier que le comportement de complétion de DMA — le message posté au fil
   demandeur — reste identique à celui de l'hôte moderne.

## Critères d'acceptation

- [ ] L'empreinte mémoire de la ROM respecte le budget de E00-S06.
- [ ] Le jeu charge et démarre depuis une ROM valide sous Windows 95 émulé.
- [ ] Le temps de chargement d'un niveau est mesuré et comparé à celui de l'hôte
      moderne.
- [ ] La durée de validation SHA-1 est mesurée ; si elle est perceptible, elle est
      accompagnée d'un retour utilisateur.
- [ ] Les trois formats de ROM sont acceptés sans charger l'image entière si la
      lecture par morceaux est retenue.
- [ ] Les chemins Windows 9x sont gérés, y compris avec des noms courts.
- [ ] La sémantique de complétion de DMA est inchangée.

## Risques

La lecture par morceaux introduit une latence là où il n'y en avait pas. Si un
transfert est servi pendant une image de jeu plutôt que pendant un écran de
chargement, il produit un à-coup. Repérer les transferts en cours de partie avant
de choisir la stratégie, pas après.

## Références

- `docs/ROM_SETUP.md`
- `README.md` — SHA-1 attendu après normalisation
- `../../Diddy-Kong-Racing/docs/stories/E02-assets/E02-S04-chargeur-assets-fichier.md`
  — même problème, déjà traité côté portage natif
