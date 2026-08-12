# E05-S06 — Brouillard

| | |
|---|---|
| **Épic** | E05 — Backend Glide |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | S |
| **Dépend de** | E04-S06, E05-S01 |
| **Bloque** | — |

## Contexte

DKR utilise le brouillard de façon visible : il masque la limite de distance
d'affichage, et il participe à l'ambiance de plusieurs niveaux. Un brouillard
absent ou mal calibré ne se traduit pas par une image « un peu différente » mais
par des objets qui apparaissent brutalement au loin.

La N64 calcule le brouillard par sommet, dans le RSP, et le RDP l'applique via le
combineur. Glide procède autrement : il dispose d'une unité de brouillard dédiée,
pilotée par une **table de 64 entrées** (`grFogTable`) indexée par la profondeur,
plus une couleur de brouillard. Un mode permet aussi de fournir directement le
facteur par sommet, ce qui correspond mieux au modèle de la N64.

Cette seconde voie est probablement la bonne : le facteur de brouillard est déjà
calculé par sommet dans le pipeline (E04-S03), et la fournir directement évite de
devoir reconstruire une table équivalente à la courbe du jeu.

## Objectif

Reproduire le brouillard de DKR sur Glide, avec une transition visuellement
identique à la référence.

## Périmètre

**Dans :** le calcul du facteur de brouillard et son application.

**Hors :** la distance d'affichage elle-même, qui relève du jeu.

## Travail

1. Relever comment le microcode calcule le facteur de brouillard par sommet, et
   comment le combineur l'applique. Le decomp est la source de référence ici.
2. Trancher entre les deux voies Glide : facteur par sommet, ou table indexée par
   la profondeur. La première correspond mieux au modèle de la N64 ; mesurer si
   elle a un coût par sommet significatif avant de conclure.
3. Si la table est retenue, la construire à partir de la courbe réelle du jeu, et
   mesurer l'erreur d'approximation introduite par ses 64 entrées.
4. Implémenter la couleur de brouillard, qui varie selon le niveau : elle est
   fournie par l'état du jeu, pas fixée à la construction.
5. Vérifier l'interaction avec le mélange et le test alpha (E05-S05) : une surface
   translucide dans le brouillard est un cas où les ordres d'application diffèrent
   entre le RDP et Glide.
6. Comparer visuellement à la référence sur les niveaux où le brouillard est le
   plus marqué, et sur une caméra qui s'éloigne progressivement — c'est la
   progression de la transition qui révèle une courbe fausse, pas une capture
   fixe.
7. Mesurer le coût, qui devrait être négligeable puisque l'unité est matérielle.

## Critères d'acceptation

- [ ] Le calcul du facteur de brouillard du microcode est relevé depuis le decomp.
- [ ] Le choix facteur par sommet / table est justifié par une mesure.
- [ ] Si la table est retenue, son erreur d'approximation est mesurée.
- [ ] La couleur de brouillard suit l'état du jeu et varie correctement d'un
      niveau à l'autre.
- [ ] L'interaction avec le mélange et le test alpha est vérifiée.
- [ ] La transition est comparée à la référence sur une caméra qui s'éloigne, et
      non sur une capture fixe.
- [ ] Le coût est mesuré.

## Risques

Une courbe de brouillard fausse est difficile à repérer sur une capture unique et
saute aux yeux en mouvement. La vérification de l'étape 6 doit donc être une
séquence, pas une image.

## Références

- E04-S06 — état de brouillard dans l'inventaire RDP
- E04-S03 — facteur par sommet, calculé en amont
- [Sources Glide 3dfx](https://sourceforge.net/projects/glide/) — `grFogTable`,
  `grFogMode`
