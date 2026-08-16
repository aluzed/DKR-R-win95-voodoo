# E06-S05 — Configuration par fichier

| | |
|---|---|
| **Épic** | E06 — Plateforme Windows 95 |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | S |
| **Dépend de** | E06-S01 |
| **Bloque** | E06-S06, E09-S05 |

## Contexte

DKR-R expose ses réglages par un overlay ImGui : `runtime_ui.cpp` fait 194 Ko. Cet
overlay disparaît avec ImGui (E07-S02), et il faut le remplacer.

Sur cette cible, un fichier de configuration en texte est le bon choix, pour trois
raisons : c'est l'usage de l'époque, cela ne coûte rien en mémoire ni en CPU, et
cela reste diagnosticable — un utilisateur peut lire et corriger son fichier, ce
qui compte quand le support se fait à distance sur du matériel qu'on n'a pas.

Le format `.ini` est celui de Windows 95, et le système fournit les fonctions pour
le lire — `GetPrivateProfileString` et sa famille, présentes depuis Windows 3.1.
Autant s'en servir plutôt que d'écrire un analyseur.

## Objectif

Livrer une configuration par fichier `.ini` couvrant tous les réglages du portage,
avec des valeurs par défaut adaptées à la cible.

## Périmètre

**Dans :** lecture, écriture, valeurs par défaut, validation, documentation.

**Hors :** toute interface graphique de configuration.

## Travail

1. Inventorier les réglages à conserver, en repartant de `runtime_ui.cpp` et en
   retirant ceux qui disparaissent avec le profil « Accurate » seul (E07-S01).
2. Concevoir le fichier par sections : affichage, rendu, audio, entrées, chemins,
   diagnostic. Un fichier lisible se maintient ; un fichier plat ne se relit pas.
3. Implémenter la lecture par les fonctions système de profil, avec une valeur par
   défaut pour chaque entrée. Un fichier absent doit produire une configuration
   valide, pas une erreur.
4. Valider chaque valeur lue et se replier sur le défaut en cas de valeur aberrante,
   en le journalisant. Un fichier édité à la main contiendra des erreurs.
5. Écrire le fichier au premier lancement, commenté, avec les valeurs par défaut.
   C'est la documentation la plus efficace : elle est là où l'utilisateur regarde.
6. Choisir les réglages qui doivent être modifiables sans redémarrage, et ceux qui
   n'ont pas à l'être. Sur cette cible, exiger un redémarrage est acceptable et
   évite beaucoup de complexité.
7. Documenter chaque réglage dans `docs/CONFIGURATION.md` : effet, valeurs
   admises, défaut, et impact sur les performances quand il y en a un.
8. Prévoir les réglages de diagnostic dont les autres tickets ont besoin : mode
   trace du décodeur (E04-S02), choix du backend de rendu (E04-S08), forçage du
   chemin multipasse (E05-S04), affichage des compteurs (E08-S01).

## Critères d'acceptation

- [ ] Le fichier `.ini` couvre tous les réglages conservés.
- [ ] Un fichier absent produit une configuration valide par défaut.
- [ ] Une valeur aberrante est rejetée, remplacée par le défaut, et journalisée.
- [ ] Le fichier écrit au premier lancement est commenté.
- [ ] `docs/CONFIGURATION.md` documente chaque réglage, avec son impact sur les
      performances le cas échéant.
- [ ] Les réglages de diagnostic demandés par les autres tickets sont présents.
- [ ] L'emplacement du fichier est cohérent avec celui des sauvegardes (E02-S05).

## Risques

Le fichier de configuration est aussi ce qui permettra à un utilisateur de
diagnostiquer lui-même un problème sur une machine que le développeur n'a pas sous
la main. Ses valeurs par défaut doivent donc être sûres et son format tolérant :
une configuration qui empêche le jeu de démarrer et qu'on ne peut pas corriger est
un cul-de-sac.

## Références

- `runtime-recomp/src/game/runtime_ui.cpp` — 194 Ko d'overlay ImGui, remplacé
- `runtime-recomp/src/game/runtime_enhancements.cpp` — réglages actuels
- E07-S01 — réglages supprimés avec le profil Accurate seul
