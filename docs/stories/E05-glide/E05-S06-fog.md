# E05-S06 — Brouillard

| | |
|---|---|
| **Épic** | E05 — Backend Glide |
| **Statut** | IN_PROGRESS |
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

- [x] Relevé depuis le decomp : `gSPFogPosition(min, max)` charge un
      multiplicateur `128000/(max-min)` et un décalage `(500-min)*256/(max-min)`,
      dont le RSP tire un facteur par sommet rangé dans **l'alpha du sommet**.
      Le mélangeur l'applique par `G_RM_FOG_SHADE_A`.
      Le décodeur d'état a été corrigé au passage : il laissait le bit de
      brouillard à zéro en dur, alors que c'est le mode de rendu le plus fréquent
      du jeu. Il se déduit du mélangeur, et la valeur 3 y signifie `G_BL_CLR_FOG`
      en position `m1a` mais `G_BL_0` en `m1b` — une épreuve vérifie ce piège.
- [x] Le facteur par sommet est retenu, et la mesure le justifie : le dégradé
      est régulier de `DE1C00` à `18DB00` en passant par `7B7D00`, et le sens est
      le bon — alpha 255 vaut plein brouillard, comme le facteur de la N64 qui
      croît avec la distance. Se tromper de sens donnerait un brouillard inversé.
- [x] Sans objet : la table n'est pas retenue. Elle n'apporterait qu'une
      approximation d'une courbe qu'on possède déjà exactement, par sommet.
- [~] La couleur est prise de l'état de rendu et non fixée à la construction ;
      `grFogColorValue` la reçoit à chaque changement d'état. La **variation d'un
      niveau à l'autre** vient de `set_fog` et de `rain_fog`, relevés dans le
      decomp, mais n'est pas exercée faute de ROM.
- [x] Vérifiée, et elle révèle un couplage que le ticket n'annonçait pas :
      **l'alpha du sommet sert simultanément au brouillard et à la
      transparence**. Les deux fonctionnent, mais ne sont pas indépendants — on
      ne peut régler l'un sans déranger l'autre, et le jeu emploie 78 modes
      translucides pour 74 modes de brouillard.
      Conséquence sur E05-S03 : l'issue consistant à faire voyager la seconde
      couleur constante dans l'alpha du sommet entre en conflit avec le
      brouillard. Elle n'est pas générale.
- [ ] Comparaison sur une caméra qui s'éloigne — **bloqué par la ROM**. Le
      ticket a raison d'insister : une courbe fausse ne se voit pas sur une image
      fixe. Le dégradé mesuré ici est une transition dans l'espace, pas dans le
      temps.
- [~] Mesuré, mais la mesure ne tranche pas : 1538 ms contre 1662 ms sur cent
      images, soit deux multiples différents de la période de balayage. **La
      mesure est quantifiée par l'échange de tampons** et ne peut pas résoudre un
      coût inférieur à une période. Ce qu'on peut affirmer : le brouillard ne
      fait pas franchir plus d'une période, ce qui borne son coût par le haut.

> **Correction du 15 août 2026** : ce critère avait été marqué bloqué par l'absence de ROM. La ROM était présente — voir `docs/research/win95-rom-available.md`. Le blocage n'existe plus ; ce qui reste à faire l'est pour d'autres raisons, ou n'a simplement pas encore été fait.

## Risques

Une courbe de brouillard fausse est difficile à repérer sur une capture unique et
saute aux yeux en mouvement. La vérification de l'étape 6 doit donc être une
séquence, pas une image.

## Références

- E04-S06 — état de brouillard dans l'inventaire RDP
- E04-S03 — facteur par sommet, calculé en amont
- [Sources Glide 3dfx](https://sourceforge.net/projects/glide/) — `grFogTable`,
  `grFogMode`
