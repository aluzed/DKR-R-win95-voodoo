# E04-S03 — Pile de matrices et transformation des sommets

| | |
|---|---|
| **Épic** | E04 — HLE F3DDKR indépendant de RT64 |
| **Statut** | IN_PROGRESS |
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

- [x] La pile de matrices reproduit le comportement du microcode, profondeur
      **relevée et non supposée** : `f3ddkr_rt64.cpp` borne l'index à 2 dans
      `Matrix` comme dans `MoveWord`, donc trois emplacements. En prévoir seize
      par prudence coûterait de la mémoire sur une machine qui n'en a pas, et
      masquerait une commande mal décodée visant un emplacement inexistant.
- [~] La conversion 16.16 en deux moitiés est exacte, testée sur des valeurs
      **calculées à la main** — entier seul, fraction seule, négatif avec
      fraction, et juste sous l'unité. Le cas qui casse une conversion naïve est
      couvert : la fraction n'est pas signée. **Pas de matrices capturées** :
      elles demandent la ROM.
- [~] Le choix flottant / virgule fixe est justifié par une mesure consignée —
      **le x87 est mesuré, la virgule fixe ne l'est pas**. Écrire une variante à
      la hâte mesurerait sa propre maladresse plutôt que la technique, et la
      décision dépend du nombre de sommets par image, que seul le jeu donne.
      Voir [`docs/research/win95-vertex-cost.md`](../../research/win95-vertex-cost.md).
- [x] Les sommets transformés sont produits directement au format du backend,
      sans recopie — `dkr_render_vertex` est écrit sur place, `oow` et `ooz`
      compris.
- [ ] Les positions écran correspondent à celles de la cible moderne — **bloqué**,
      la comparaison demandant des scènes capturées, donc la ROM.
- [x] Le coût par sommet et par image est mesuré sur la cible : **0,682 µs par
      sommet**, soit **24 322 sommets** dans une image de 16,6 ms.
- [ ] L'éclairage par sommet — **pas fait**. DKR porte des couleurs de sommet, et
      le décodeur les transmet telles quelles ; savoir si le microcode y applique
      autre chose demande de relever le comportement dans le decomp, qui n'est
      pas dans ce dépôt.

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
