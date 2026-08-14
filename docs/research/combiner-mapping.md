# Correspondance combineur RDP → Glide

Engendré par `tools/win95/gen_combiner_table.py`. Les définitions viennent de
`include/PR/gbi.h` et `include/f3ddkr.h` du portage voisin ; la liste des
configurations employées vient de `combiner-inventory.md`, lui-même engendré
depuis les tables statiques du jeu.

## La correspondance structurelle

Glide possède `GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL`, qui
calcule `f × (other − local) + local`. C'est **exactement** la forme du RDP,
`(a − b) × c + d`, dès lors que `d = b`. La quasi-totalité des configurations de
DKR vérifie cette égalité — ce n'est pas une chance : les deux matériels
expriment la même intention, interpoler entre deux couleurs.

## Les deux murs

**Un seul registre de couleur constante.** Le RDP a `PRIMITIVE` et
`ENVIRONMENT` ; Glide n'a que `grConstantColorValue`. Une configuration qui lit
les deux dans le même terme est hors d'atteinte en une passe.

Mais il ne faut pas déclarer ce mur trop tôt. Glide a un combineur de couleur et
un combineur d'alpha **séparés**, et la valeur d'alpha d'un sommet nous
appartient — la chaîne l'écrit. Si le terme de couleur ne lit qu'un registre et
le terme d'alpha l'autre, le second peut être **porté par la couleur du
sommet** : les deux sont constants par appel de dessin, donc connus du
processeur au moment d'écrire les sommets. Cela ne coûte rien et évite une passe
entière. Le prix est nommé : l'alpha du sommet ne peut plus porter autre chose,
donc la manœuvre ne s'applique que si le terme d'alpha ne lit pas `SHADE`.

**`TEXEL1`.** Trois configurations lisent deux texels. Ce n'est pas une
approximation mais un renvoi : c'est la seconde TMU, donc E05-S04.

## Le second cycle se replie plus souvent qu'on ne croit

Le ticket nomme lui-même la sortie — « simplification quand le second étage est
neutre » — et deux cas se replient :

- `G_CC_PASS2` vaut `(0,0,0,COMBINED)`, c'est-à-dire **l'identité** ;
- un second étage de la forme `(COMBINED, 0, X, 0)` est une mise à l'échelle du
  résultat précédent, qui compose en un simple `SCALE_OTHER`.

Sans ce repliage, tout second cycle serait déclaré multipasse, ce qui doublerait
le remplissage sur les surfaces les plus courantes du jeu pour rien — et le
remplissage est précisément ce qui limite une Voodoo 2 en 640×480.

## Le résultat

| Catégorie | Configurations | Entrées de table | Part |
|---|---|---|---|
| exacte | 15 | 108 | 50 % |
| multipasse | 10 | 71 | 33 % |
| deux texels | 3 | 34 | 15 % |
| approchee | 1 | 1 | 0 % |

**La moitié du jeu, en poids d'entrées de table, passe en une seule passe exacte.**

## Le détail, par poids décroissant

| Poids | Cycle 1 | Cycle 2 | Catégorie | Constante | Pourquoi |
|---|---|---|---|---|---|
| 32 | `G_CC_BLENDTEX_PRIM` | `G_CC_MODULATEIDECALA2` | deux texels | — | lit TEXEL1 : renvoye a E05-S04, seconde TMU |
| 32 | `G_CC_MODULATEIDECALA` | — | exacte | — | forme (a-b)c+d avec d=b, facteur exprimable |
| 20 | `G_CC_MODULATEIDECALA` | `G_CC_BLENDI_ENV_ALPHA_PRIM2` | multipasse | ENVIRONMENT | second cycle irreductible : un etage de plus que Glide n'en offre |
| 20 | `G_CC_MODULATEIDECALA` | `G_CC_PASS2` | exacte | — | second etage neutre (PASS2) : se replie sur le premier |
| 18 | `G_CC_MODULATEIA_PRIM` | — | exacte | PRIMITIVE | forme (a-b)c+d avec d=b, facteur exprimable |
| 16 | `G_CC_BLEND_SHADEALPHA` | `G_CC_BLENDI_SHADE` | multipasse | les deux | lit PRIMITIVE et ENVIRONMENT dans le meme terme : Glide n'a qu'un registre constant |
| 9 | `G_CC_PRIMITIVE` | — | exacte | PRIMITIVE | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 8 | `G_CC_MODULATEIDECALA` | `G_CC_MODULATEIA_PRIM` | multipasse | PRIMITIVE | second cycle irreductible : un etage de plus que Glide n'en offre |
| 8 | `G_CC_BLENDI_ENV_ALPHA_A_PRIM` | — | exacte | ENVIRONMENT | forme (a-b)c+d avec d=b, facteur exprimable |
| 8 | `G_CC_MODULATEIA_PRIM` | `G_CC_BLEND_ENV_ALPHA2` | multipasse | les deux | lit PRIMITIVE et ENVIRONMENT dans le meme terme : Glide n'a qu'un registre constant |
| 8 | `G_CC_BLENDI_ENV_ALPHA_A_PRIM` | `G_CC_MODULATEIA_PRIM2` | multipasse | les deux | lit PRIMITIVE et ENVIRONMENT dans le meme terme : Glide n'a qu'un registre constant |
| 8 | `G_CC_DECAL_A_PRIM` | — | exacte | PRIMITIVE | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 4 | `G_CC_MODULATERGBA` | `G_CC_MODULATEA_PRIM2` | exacte | PRIMITIVE | second etage neutre (PASS2) : se replie sur le premier |
| 4 | `G_CC_MODULATERGBA` | `G_CC_BLENDI_ENV_ALPHA_PRIM2` | multipasse | les deux | lit PRIMITIVE et ENVIRONMENT dans le meme terme : Glide n'a qu'un registre constant |
| 4 | `G_CC_BLENDI_ENV_ALPHA` | `G_CC_MODULATEIA_PRIM2` | multipasse | les deux | lit PRIMITIVE et ENVIRONMENT dans le meme terme : Glide n'a qu'un registre constant |
| 2 | `G_CC_SHADE` | — | exacte | — | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 1 | `G_CC_BLENDPE_A_PRIM` | — | multipasse | les deux | lit PRIMITIVE et ENVIRONMENT dans le meme terme : Glide n'a qu'un registre constant |
| 1 | `G_CC_BLENDPE_A_PRIM` | `G_CC_PASS2` | multipasse | les deux | lit PRIMITIVE et ENVIRONMENT dans le meme terme : Glide n'a qu'un registre constant |
| 1 | `G_CC_BLENDPE` | — | multipasse | les deux | lit PRIMITIVE et ENVIRONMENT dans le meme terme : Glide n'a qu'un registre constant |
| 1 | `G_CC_BLENDT_ENV_ALPHA_A_TxP` | — | exacte | ENVIRONMENT | forme (a-b)c+d avec d=b, facteur exprimable |
| 1 | `G_CC_ENVIRONMENT` | — | exacte | ENVIRONMENT | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 1 | `G_CC_BLENDT_ENV_ALPHA_A_T1xP` | `G_CC_PASS2` | deux texels | — | lit TEXEL1 : renvoye a E05-S04, seconde TMU |
| 1 | `G_CC_MODULATEIA` | — | exacte | — | forme (a-b)c+d avec d=b, facteur exprimable |
| 1 | `G_CC_ENV_DECALA` | — | exacte | ENVIRONMENT | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 1 | `G_CC_DECALRGB` | — | exacte | — | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 1 | `G_CC_DECALRGBA` | — | exacte | — | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 1 | `G_CC_DECAL_SCALE` | — | approchee | — | SCALE : registre de mise a l'echelle du RDP, sans equivalent |
| 1 | `G_CC_BLENDTEX_MODULATEA_1_PRIM` | `G_CC_BLENDI_ENV_ALPHA_MODULATEA2` | deux texels | — | lit TEXEL1 : renvoye a E05-S04, seconde TMU |
| 1 | `G_CC_BLENDT_ENV_ALPHA_A_PRIM` | `G_CC_MODULATEIDECALA2` | exacte | ENVIRONMENT | second etage = mise a l'echelle : se replie en SCALE_OTHER |

## Ce qui reste à mesurer

L'écart de chaque configuration approchée ou multipasse au rastériseur de
référence, par différence d'image sur la machine. Le rastériseur évalue désormais
`(a − b) × c + d` fidèlement — ce n'était pas le cas avant ce ticket, et le
critère de mesure n'avait donc pas de sens.

## Une configuration inconnue

Journalisée, puis rendue par un repli **visible mais non aberrant** : texture
modulée par la couleur du sommet, le comportement le plus fréquent de
l'inventaire, donc celui qui a le plus de chances d'être juste sur une
configuration imprévue.

Les deux réflexes opposés sont écartés : ne rien dessiner ferait disparaître un
décor sans laisser de trace, et peindre en couleur d'alerte rendrait le jeu
injouable au premier combineur oublié.
