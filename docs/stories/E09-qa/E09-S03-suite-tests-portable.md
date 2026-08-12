# E09-S03 — Suite de tests portable

| | |
|---|---|
| **Épic** | E09 — Intégration, QA et distribution |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E00-S07, E01-S01, E07-S03 |
| **Bloque** | E09-S05 |

## Contexte

Le projet dispose de 18 suites de tests exécutées par CTest, et les scripts de
build les exigent avant tout empaquetage (`Build-Linux.sh:32`). C'est une bonne
discipline, qu'il faut conserver malgré trois difficultés :

- une partie de ces suites porte sur des politiques modernes qui disparaissent
  (E07-S01, E07-S02) ;
- la cible Win95 est cross-compilée : ses tests ne s'exécutent pas sur l'hôte de
  build sans passer par la machine émulée ;
- CTest et le cadre de test employé peuvent ne pas être disponibles dans le
  sous-ensemble C++ retenu (E01-S02).

La bonne réponse est de séparer trois familles : ce qui teste de la **logique
portable** et tourne partout, ce qui teste du **code spécifique à la plateforme**
et doit tourner sur la cible, et ce qui **compare** les deux cibles.

## Objectif

Conserver une vérification automatique utile sur les deux cibles, exécutée avant
tout empaquetage.

## Périmètre

**Dans :** l'organisation des tests, leur exécution sur la cible, leur intégration
aux scripts de build.

**Hors :** la comparaison visuelle (E09-S02).

## Travail

1. Trier les 18 suites existantes selon la décision de E00-S07 : conservées,
   retirées, à adapter. Les suites de codec de sauvegarde et d'égaliseur audio sont
   à conserver ; celles des politiques modernes sortent.
2. Classer les tests conservés en trois familles : logique portable, spécifique
   plateforme, comparaison inter-cibles.
3. Faire tourner les tests de logique portable sur les deux cibles. S'ils
   dépendent d'un cadre de test indisponible en C++ restreint, prévoir un
   remplacement minimal plutôt que d'y renoncer.
4. Écrire les tests de plateforme réclamés par E02 : fils et synchronisation
   (E02-S01), horloge et débordement (E02-S03), sauvegardes (E02-S05).
5. Automatiser l'exécution sur la machine émulée : lancer la suite, récupérer les
   résultats, les rapporter sur l'hôte. Sans automatisation, ces tests ne seront
   pas exécutés régulièrement, et des tests qu'on n'exécute pas ne valent rien.
6. Ajouter les tests de comparaison entre cibles : la même entrée produit-elle le
   même résultat sur l'hôte moderne et sur la cible ? C'est ce qui attrapera les
   divergences d'arrondi flottant introduites par `-mfpmath=387` (E01-S01).
7. Intégrer aux scripts de build, en échec bloquant, sur le modèle de
   `Build-Linux.sh`.
8. Documenter l'exécution dans `docs/TESTING.md`.

## Critères d'acceptation

- [ ] Les 18 suites existantes sont triées, chaque décision étant justifiée.
- [ ] Les tests de logique portable passent sur les deux cibles.
- [ ] Les tests de plateforme réclamés par E02 sont écrits et passent sur la cible.
- [ ] L'exécution sur la machine émulée est automatisée.
- [ ] Les tests de comparaison inter-cibles détectent une divergence d'arrondi
      introduite volontairement.
- [ ] Les tests sont bloquants dans les scripts de build.
- [ ] `docs/TESTING.md` documente l'exécution sur les deux cibles.

## Risques

Une suite de tests qui ne tourne que sur l'hôte moderne donne une confiance
trompeuse : elle valide un code compilé par un autre compilateur, pour une autre
architecture, avec une autre arithmétique flottante. L'automatisation de l'étape 5
est ce qui distingue une suite utile d'une suite décorative.

## Références

- `runtime-recomp/tests/` — 18 suites existantes
- `Build-Linux.sh:32` — CTest bloquant avant empaquetage
- E01-S01 — `-mfpmath=387` et ses écarts d'arrondi
