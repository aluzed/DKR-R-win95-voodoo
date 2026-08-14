# Le brouillard

Relevé dans le decomp et mesuré le 14 août 2026 sur la machine d'épreuve, par
`tools/win95/witnesses/fog_probe.c`.

## Le modèle de la N64, relevé et non supposé

`gSPFogPosition(min, max)` de `gbi.h` charge deux valeurs :

    multiplicateur = 128000 / (max - min)
    decalage       = (500 - min) * 256 / (max - min)

Le RSP en tire un facteur par sommet qu'il range dans **l'alpha du sommet**. Le
mélangeur l'applique ensuite par `G_RM_FOG_SHADE_A`, défini comme
`GBL_c1(G_BL_CLR_FOG, G_BL_A_SHADE, G_BL_CLR_IN, G_BL_1MA)` : mélange la couleur
de brouillard avec le pixel, dosé par l'alpha du sommet.

Le jeu appelle `set_fog(index, near, far, r, g, b)` depuis l'en-tête de niveau, et
la météo le remplace en course — `rain_fog()` recalcule near et far selon
l'intensité de l'orage. La couleur de brouillard vient donc de l'état du jeu et
n'est jamais fixée à la construction.

## Le brouillard n'a pas de bit propre

Il se déduit de la configuration du mélangeur : source de couleur `G_BL_CLR_FOG`
en position `m1a`, facteur `G_BL_A_SHADE` en position `m1b`. Notre décodeur
laissait ce bit à zéro en dur, alors que `G_RM_FOG_SHADE_A` est **le mode de
rendu le plus fréquent de DKR** — 74 occurrences dans la source.

Et l'on retrouve le piège du RDP : la valeur 3 signifie `G_BL_CLR_FOG` en
position `m1a` mais `G_BL_0` en position `m1b`. Lire les deux avec le même
dictionnaire déclarerait du brouillard là où il n'y en a pas. Une épreuve
vérifie explicitement ce cas.

## La voie retenue : le facteur par sommet

Le ticket demande de trancher entre `GR_FOG_WITH_ITERATED_ALPHA` et la table de
64 entrées. La mesure tranche sans hésitation :

    couleur de brouillard vert, surface rouge, alpha de 0 a gauche a 255 a droite

    sans brouillard : FF0000 partout — l'alpha du sommet n'y change rien
    avec           : DE1C00 a gauche, 7B7D00 au milieu, 18DB00 a droite

Le dégradé est régulier et le sens est le bon : alpha 255 vaut plein brouillard,
ce qui correspond au facteur de la N64, lequel croît avec la distance. Se
tromper de sens donnerait un brouillard **inversé** — opaque de près, clair au
loin — spectaculaire, et facile à attribuer à la courbe plutôt qu'au sens.

La table de 64 entrées n'est donc pas construite : elle n'apporterait qu'une
approximation d'une courbe qu'on possède déjà exactement, par sommet. La question
de son erreur d'approximation devient sans objet.

## Ce que la mesure a révélé au-delà de la question posée

**L'alpha du sommet sert simultanément au brouillard et à la transparence.**

Mesuré en activant les deux à la fois, sur fond bleu :

    gauche 0x1804DE — le bleu du fond transparait : la surface est translucide
    droite 0x18BE18 — vert : plein brouillard

Les deux usages fonctionnent, et c'est précisément le problème : ils sont
**couplés**. Une surface translucide dans le brouillard tire sa transparence et
son dosage de brouillard de la même valeur, et l'on ne peut pas régler l'un sans
déranger l'autre. Le jeu emploie 78 modes translucides pour 74 modes de
brouillard : la rencontre est certaine.

### Une conséquence sur E05-S03

E05-S03 proposait de faire voyager la seconde couleur constante dans l'alpha du
sommet, pour contourner l'unique registre constant de Glide. **Cette issue entre
en conflit avec le brouillard**, qui occupe déjà cette place sur 74 modes de
rendu.

L'issue n'est donc pas générale. Elle reste envisageable sur les configurations
sans brouillard, ce qui la rend conditionnelle plutôt qu'impossible — mais la
classification de E05-S03 ne doit pas s'appuyer dessus sans le dire.

Il vaut mieux avoir découvert cela ici, sur un témoin, que sur un décor faux.

## Le coût, et pourquoi la mesure ne le résout pas

    100 images sans brouillard : 1538 ms
    100 images avec            : 1662 ms

Soit 8 %. Le ticket attendait un coût négligeable, « puisque l'unité est
matérielle ». Ce chiffre ne permet pas de le confirmer ni de l'infirmer : les
deux durées correspondent à 15,4 ms et 16,6 ms par image, c'est-à-dire à des
multiples différents de la période de balayage. **La mesure est quantifiée par
l'échange de tampons**, et ne peut pas résoudre un coût inférieur à une période.

Ce qu'on peut affirmer : le brouillard ne fait pas franchir plus d'une période, ce
qui borne son coût par le haut. Le mesurer finement demanderait de désactiver la
synchronisation, ou une scène assez chargée pour sortir du régime synchronisé.

## Ce qui reste ouvert

- La comparaison de la transition à la référence **sur une caméra qui s'éloigne**.
  Le ticket a raison d'insister : une courbe fausse ne se voit pas sur une image
  fixe. Cela demande la ROM.
- Le comportement à choisir quand brouillard et transparence se disputent l'alpha
  du sommet. La mesure dit qu'ils coexistent ; elle ne dit pas ce que le jeu
  attend.
