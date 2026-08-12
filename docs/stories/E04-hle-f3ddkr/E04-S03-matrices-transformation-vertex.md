# E04-S03 — Pile de matrices et transformation des sommets

| | |
|---|---|
| **Épic** | E04 — HLE F3DDKR indépendant de RT64 |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E04-S02 |
| **Bloque** | E04-S04, E04-S05, E08-S03 |

## Contexte

Sur la N64, la transformation des sommets est faite par le RSP. Ici, elle revient
au CPU hôte — comme sur toute carte 3dfx, qui ne transforme rien. C'est le poste
de calcul graphique le plus lourd du portage, et il tombe intégralement dans le
budget mesuré par E00-S03.

Trois particularités du microcode de Rare compliquent l'exercice :

- les matrices de la N64 sont en **virgule fixe 16.16**, stockées en deux moitiés
  séparées — parties hautes puis parties basses. Ce n'est pas un format de
  matrice ordinaire, et la conversion doit être exacte ;
- F3DDKR adresse matrices et sommets par des **décalages relatifs** fournis par la
  commande `DMAOffsets` ;
- les sommets de Rare sont dans un format compact propre au microcode, que
  `f3ddkr_rt64.cpp` traduit aujourd'hui dans une fenêtre de mémoire dédiée
  (`0x7FE000..0x7FFFFF` de l'instantané, d'après
  `docs/RENDER_SNAPSHOT_ARCHITECTURE.md`).

## Objectif

Transformer les sommets de l'espace objet vers les coordonnées écran attendues par
le backend, avec une précision suffisante et dans le budget.

## Périmètre

**Dans :** pile de matrices, transformation, projection, division perspective,
éclairage sommet.

**Hors :** le découpage (E04-S05) et le billboarding (E04-S04), qui s'insèrent
dans ce pipeline mais se traitent à part.

## Travail

1. Implémenter la pile de matrices : empilement, dépilement, chargement,
   multiplication, avec la profondeur réellement utilisée par DKR — à relever
   plutôt qu'à supposer généreusement.
2. Implémenter la conversion virgule fixe 16.16 → flottant, en respectant la
   disposition en deux moitiés. Tester par comparaison à des matrices capturées
   depuis la cible moderne.
3. Trancher la représentation interne : virgule flottante x87, ou virgule fixe.
   Le x87 du Pentium II est correct et son usage simplifie beaucoup le code ; la
   virgule fixe peut être plus rapide sur certaines opérations. Décider **sur une
   mesure**, pas sur une préférence, et consigner le chiffre.
4. Implémenter la transformation des sommets : matrice modèle-vue-projection,
   division perspective, mise à l'échelle vers les coordonnées écran, calcul de
   la valeur de profondeur.
5. Produire directement le format de vertex attendu par le backend (E04-S01) :
   Glide veut des coordonnées écran, l'inverse de la profondeur et l'inverse de
   la coordonnée homogène. Éviter toute recopie intermédiaire.
6. Implémenter l'éclairage par sommet tel que le fait le microcode : DKR utilise
   des couleurs de sommet et un éclairage simple. Relever le comportement exact
   dans le decomp plutôt que de supposer un modèle standard.
7. Mesurer le coût par sommet et par image sur la cible, et l'inscrire au budget de
   E08-S01. Ce chiffre déterminera si E08-S03 (optimisation MMX du chemin sommet)
   est nécessaire.
8. Vérifier la précision : comparer les positions écran obtenues à celles de la
   cible moderne sur des scènes capturées, et fixer une tolérance justifiée.

## Critères d'acceptation

- [ ] La pile de matrices reproduit le comportement du microcode, profondeur
      relevée sur le jeu réel.
- [ ] La conversion 16.16 en deux moitiés est exacte, testée sur des matrices
      capturées.
- [ ] Le choix flottant / virgule fixe est justifié par une mesure consignée.
- [ ] Les sommets transformés sont produits directement au format du backend, sans
      recopie.
- [ ] Les positions écran correspondent à celles de la cible moderne, dans une
      tolérance écrite et justifiée.
- [ ] Le coût par sommet et par image est mesuré sur la cible.
- [ ] L'éclairage par sommet correspond au comportement du microcode, vérifié
      contre le decomp.

## Risques

Un écart de précision se voit immédiatement à l'écran, sous forme de tremblement
de géométrie ou de fissures entre polygones adjacents. La N64 travaillait en
virgule fixe avec des règles d'arrondi précises ; reproduire son comportement en
flottant introduit des écarts qui, isolément, sont invisibles, et qui, cumulés sur
une chaîne de matrices, ne le sont plus. La tolérance de l'étape 8 doit être
mesurée sur la géométrie d'un niveau complet, pas sur un cube de test.

## Références

- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — gestionnaires `Matrix`, `Vertex`,
  `DMAOffsets`
- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md` — fenêtre de traduction des sommets
- `extern/dkr-decomp` — `include/f3ddkr.h`, pile de matrices du microcode
