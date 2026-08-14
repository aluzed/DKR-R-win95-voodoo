# Ce que la TMU exige — mesuré avant de concevoir l'allocateur

Relevé le 14 août 2026 sur la machine d'épreuve (Voodoo 2, 2 TMU), par
`tools/win95/witnesses/tmu_probe.c`.

## Pourquoi mesurer plutôt que supposer

Glide n'a pas de gestionnaire de textures. Elle expose la mémoire de la TMU
comme un espace d'adressage brut : on choisit une adresse, on y télécharge, on
lie cette adresse au dessin. Allocation, fragmentation et éviction sont
entièrement à écrire.

Un allocateur qui se contenterait de `largeur × hauteur × 2` empilerait les
textures trop serré. Le symptôme ne serait pas une erreur — Glide ne valide rien
— mais **une texture qui en écrase une autre**, donc un décor portant le motif
d'un autre, à un endroit qui dépend de l'ordre de chargement.

## Le relevé

    TMU presentes    : 2
    memoire par TMU  : 2048 Ko chacune
    espace adressable: 0x00000000 .. 0x001FFFF8, identique sur les deux

`grTexMinAddress` rend **zéro**. Ce n'est pas anodin : zéro est donc une adresse
parfaitement valide, et ne peut pas servir de sentinelle d'échec. D'où
`DKR_TMU_NONE` à `0xFFFFFFFF`.

Il manque **huit octets** pour que la TMU fasse 2 Mio pleins.

### Le coût réel d'une texture

| taille | calculé | rendu par la carte |
|---|---|---|
| 256×256 | 131072 | 131072 |
| 128×128 | 32768 | 32768 |
| 64×64 | 8192 | 8192 |
| 32×32 | 2048 | 2048 |
| 8×8 | 128 | 128 |
| 2×2 | 8 | 8 |
| **1×1** | **2** | **8** ← arrondi |
| 64×32 | 4096 | 4096 |
| 8×64 | 1024 | 1024 |

Quatorze tailles sur quinze coûtent exactement ce que le calcul annonce. La
quinzième donne la granularité : **8 octets**.

Les cinq formats testés — RGB 565, ARGB 1555, ARGB 4444, alpha 8 bits,
palettisé 8 bits — coûtent exactement ce que leur profondeur annonce.

Ces chiffres valident aussi les valeurs d'énumération, écrites de mémoire faute
de `glide.h` : un `GR_LOD_*` ou un `GR_ASPECT_*` faux aurait donné une taille
visiblement fausse. Le LOD nomme la **plus grande dimension** et décroît — 256
vaut zéro, 1 vaut huit — ce qui est l'inverse de l'intuition.

## Ce que la mesure a décidé

**Toute taille de texture est une puissance de deux.** Ce n'est pas une
observation statistique mais une conséquence : les dimensions exprimables par le
couple (LOD, rapport d'aspect) sont des puissances de deux, et la profondeur vaut
un ou deux octets.

Le ticket envisageait un allocateur par classes de taille « si les textures du
jeu se répartissent en un petit nombre de tailles ». La mesure donne mieux qu'une
répartition : elle donne une propriété. Un allocateur **buddy** est exactement un
allocateur par classes de taille doté de la fusion, et sur des demandes toutes en
puissances de deux il ne produit **aucune fragmentation externe**.

C'est ce qui compte ici plus qu'ailleurs. Sur une carte sans pagination, une
fragmentation qui refuse une texture ne dégrade pas les performances : elle fait
manquer un décor.

## Les huit octets manquants coûtent 128 octets, pas 8 Kio

L'arbre couvre 2 Mio pleins ; la carte n'en offre que `0x1FFFF8`. Le haut de
l'arbre est donc réservé, en descendant jusqu'au plus petit bloc qui contient
réellement la frontière.

Conséquence mesurée par la suite d'épreuve : la TMU tient **255** textures de
64×64 et non 256 — le dernier bloc de 8 Kio est coupé, donc plus allouable en
entier. Mais sa queue reste utilisable à grain fin : 63 blocs de 128 octets s'y
placent encore. Le coût réel de la réserve est de 128 octets par TMU.

La distinction n'est pas académique : perdre 8 Kio serait le prix de quatre
textures 32×32.

## L'échelle des coordonnées de texture

Découverte en vérifiant que la texture arrive bien à l'écran, et qui dépasse
E05-S02 : **Glide attend `s` et `t` dans un espace de 256 texels, quelle que soit
la taille réelle de la texture.**

Mesuré par `glide_texture_probe.c` sur un damier 64×64 dont les quatre coins
portent des couleurs distinctes, en essayant cinq échelles :

    s,t sur   64 : HG rouge  HD rouge  BG rouge  BD rouge     (une seule case)
    s,t sur  128 : HG rouge  HD gris   BG gris   BD gris
    s,t sur  255 : HG rouge  HD vert   BG bleu   BD jaune     <-- les quatre coins
    s,t sur  256 : HG rouge  HD vert   BG bleu   BD jaune     <-- les quatre coins
    s,t sur  512 : HG gris   HD gris   BG gris   BD jaune

Soit un facteur 256/largeur, ici 4.

Le contrat retient cette convention plutôt que [0,1] pour une raison de coût : le
facteur est une **constante**, indépendante de la texture liée. Le backend Glide
reçoit donc les sommets tels quels — c'est tout l'intérêt d'avoir calqué
`GrVertex` champ pour champ — et c'est le rastériseur de référence, qui n'a pas
de contrainte de vitesse, qui divise pour retrouver du [0,1]. L'inverse aurait
imposé une copie de chaque sommet avant chaque triangle, sur une machine où la
transformation coûte déjà 0,682 µs par sommet.

Le piège que cela ferme : **les deux backends ne parlaient pas la même langue** —
le rastériseur échantillonnait en [0,1], la carte en 256 — et la comparaison de
E09-S02 ne l'a pas vu, faute de texture dans la scène.

## Un damier noir et blanc n'aurait rien prouvé

Le témoin de texture emploie quatre couleurs distinctes aux quatre coins, ce qui
vérifie d'un coup que la texture est arrivée, qu'elle est lue dans le bon sens,
que le rapport d'aspect est juste, et que le combineur prend le texel plutôt que
la couleur du sommet. Un damier symétrique aurait passé une inversion de `s` et
`t` sans la signaler — et une telle inversion fausse tout le jeu.

Le contrôle négatif compte autant : en mode couleur de sommet, l'écran doit être
uniformément blanc. Sans lui, les quatre contrôles précédents pourraient mesurer
un état hérité de l'image d'avant.

## Ce qui reste supposé

**La taille du plus petit bloc**, 128 octets, soit une texture 8×8 en 16 bits.
Descendre à la granularité matérielle de 8 octets multiplierait par seize la
table de suivi pour des textures qui n'existent probablement pas. C'est le seul
endroit de l'allocateur qui ne repose pas sur une mesure, et il tombera dès que
la ROM permettra de relever les tailles réelles.
