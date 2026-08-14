# Correspondance combineur RDP → Glide

Engendré par `tools/win95/gen_combiner_table.py`. Définitions relevées dans
`include/PR/gbi.h` et `include/f3ddkr.h` ; liste des configurations tirée de
`combiner-inventory.md`, engendré depuis les tables statiques du jeu.

Classification **mesurée sur la machine** le 14 août 2026, pas déduite.

## La correspondance structurelle

Glide possède `GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL`, qui
calcule `f × (other − local) + local`. C'est **exactement** la forme du RDP,
`(a − b) × c + d`, dès lors que `d = b`. La quasi-totalité des configurations de
DKR vérifie cette égalité — les deux matériels expriment la même intention :
interpoler entre deux couleurs.

## Ce que la carte a dit, et que la mémoire disait mal

Les valeurs d'énumération du combineur étaient les seules du projet à n'avoir
jamais été mesurées. `glide_state_probe.c` avait vérifié le mélange, la
profondeur, les ciseaux et le test alpha ; pas celles-ci. Le harnais les a prises
en défaut avec 140 à 156 unités d'écart sur toute la famille `BLENDI`/`BLENDT`.

Le balayage de `combine_enum_probe.c` a établi, valeur par valeur :

| Ce qui est mesuré | Valeur | Comportement observé |
|---|---|---|
| fonction | 1 | `LOCAL` — rend la couleur locale |
| fonction | 3 | `SCALE_OTHER` — rend `f × other` |
| fonction | 4 | `SCALE_OTHER_ADD_LOCAL` |
| fonction | 7 | `f × (other − local) + local` |
| facteur | 1 | la couleur locale |
| facteur | 8 | un |
| facteur | 9 | `1 − local` (une **couleur**, pas un scalaire) |

Et le résultat négatif, qui est le plus important : **aucune des seize valeurs de
facteur ne délivre l'alpha du registre constant**. Les facteurs confirmés sont
tous fonction de la couleur locale, ou constants.

Cela reclasse toute la famille reposant sur `ENV_ALPHA` en facteur. Elle était
déclarée exacte sur la foi de valeurs écrites de mémoire ; elle est désormais
approchée, et le restera tant qu'une issue n'aura pas été mesurée.

Une issue existe et n'est pas éprouvée : `ENV_ALPHA` est une constante connue du
processeur, donc portable dans l'alpha du sommet, où `LOCAL_ALPHA` irait la
chercher. Tant que ce n'est pas mesuré, la classification reste prudente —
annoncer exact ce qui ne l'est pas est précisément le défaut contre lequel ce
ticket met en garde.

## Le combineur tronque

Une constante de 32 ressort à 28, une de 96 à 90 : le combineur multiplie en
0..255 par 255/256 et tronque, ce qui coûte un pas de quantification. Ce n'est
pas une erreur de traduction mais le matériel, et le harnais le modélise — sans
quoi chaque configuration porterait un écart systématique de huit à neuf unités
qui noierait les vrais écarts.

## Les deux murs

**Un seul registre de couleur constante.** Le RDP a `PRIMITIVE` et
`ENVIRONMENT` ; Glide n'a que `grConstantColorValue`.

Il ne faut pas le déclarer trop tôt. Glide a des combineurs couleur et alpha
**séparés**, et l'alpha du sommet nous appartient. Si le terme de couleur ne lit
qu'un registre et le terme d'alpha l'autre, le second se porte dans le sommet :
les deux sont constants par appel de dessin, donc connus du processeur au moment
d'écrire les sommets. Le prix est nommé — l'alpha du sommet ne peut plus porter
autre chose — donc la manœuvre ne s'applique que si le terme d'alpha ne lit pas
`SHADE`.

**`TEXEL1`.** Trois configurations lisent deux texels : ce n'est pas une
approximation mais un renvoi à E05-S04.

## Le second cycle se replie plus souvent qu'on ne croit

`G_CC_PASS2` vaut `(0,0,0,COMBINED)`, c'est-à-dire l'identité ; un second étage
de la forme `(COMBINED, 0, X, 0)` est une mise à l'échelle qui compose en un
simple `SCALE_OTHER`. Sans ce repliage, tout second cycle serait multipasse, ce
qui doublerait le remplissage sur les surfaces les plus courantes du jeu — et le
remplissage est ce qui limite une Voodoo 2 en 640×480.

## Le résultat

| Catégorie | Configurations | Entrées de table | Part |
|---|---|---|---|
| exacte | 12 | 98 | 45 % |
| multipasse | 10 | 71 | 33 % |
| deux texels | 3 | 34 | 15 % |
| approchee | 4 | 11 | 5 % |

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
| 8 | `G_CC_BLENDI_ENV_ALPHA_A_PRIM` | — | approchee | ENVIRONMENT | facteur = alpha d'un registre constant : mesure sur la carte, aucun facteur Glide ne le delivre |
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
| 1 | `G_CC_BLENDT_ENV_ALPHA_A_TxP` | — | approchee | ENVIRONMENT | facteur = alpha d'un registre constant : mesure sur la carte, aucun facteur Glide ne le delivre |
| 1 | `G_CC_ENVIRONMENT` | — | exacte | ENVIRONMENT | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 1 | `G_CC_BLENDT_ENV_ALPHA_A_T1xP` | `G_CC_PASS2` | deux texels | — | lit TEXEL1 : renvoye a E05-S04, seconde TMU |
| 1 | `G_CC_MODULATEIA` | — | exacte | — | forme (a-b)c+d avec d=b, facteur exprimable |
| 1 | `G_CC_ENV_DECALA` | — | exacte | ENVIRONMENT | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 1 | `G_CC_DECALRGB` | — | exacte | — | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 1 | `G_CC_DECALRGBA` | — | exacte | — | forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul |
| 1 | `G_CC_DECAL_SCALE` | — | approchee | — | SCALE : registre de mise a l'echelle du RDP, sans equivalent |
| 1 | `G_CC_BLENDTEX_MODULATEA_1_PRIM` | `G_CC_BLENDI_ENV_ALPHA_MODULATEA2` | deux texels | — | lit TEXEL1 : renvoye a E05-S04, seconde TMU |
| 1 | `G_CC_BLENDT_ENV_ALPHA_A_PRIM` | `G_CC_MODULATEIDECALA2` | approchee | ENVIRONMENT | facteur = alpha d'un registre constant : mesure sur la carte, aucun facteur Glide ne le delivre |

## Une configuration inconnue

Journalisée, puis rendue par un repli **visible mais non aberrant** : texture
modulée par la couleur du sommet, le comportement le plus fréquent de
l'inventaire. Les deux réflexes opposés sont écartés — ne rien dessiner ferait
disparaître un décor sans trace, peindre en couleur d'alerte rendrait le jeu
injouable au premier combineur oublié.

## Ce qui reste ouvert

- L'issue par l'alpha du sommet pour la famille `ENV_ALPHA`, non mesurée.
- Le coût de remplissage du multipasse, non mesuré : il demande une scène
  représentative, donc la ROM.
- Les trois configurations à deux texels, qui appartiennent à E05-S04.

## Les écarts mesurés sur la carte

Relevé par `COMBINER.EXE` le 14 août 2026, après correction de la
classification. Écart maximal par canal, quantification 565 et troncature du
combineur déjà défalquées.

| Configuration | Catégorie | Écart |
|---|---|---|
| `G_CC_MODULATEIDECALA` | exacte | 0 |
| `G_CC_MODULATEIA_PRIM` | exacte | 0 |
| `G_CC_MODULATERGBA` | exacte | 0 |
| `G_CC_MODULATEIA` | exacte | 0 |
| `G_CC_PRIMITIVE` | exacte | 0 |
| `G_CC_SHADE` | exacte | 0 |
| `G_CC_ENVIRONMENT` | exacte | 0 |
| `G_CC_ENV_DECALA` | exacte | 0 |
| `G_CC_DECALRGB` | exacte | 8 |
| `G_CC_DECALRGBA` | exacte | 8 |
| `G_CC_DECAL_A_PRIM` | exacte | 8 |
| `G_CC_DECAL_SCALE` | approchée | 8 |
| `G_CC_BLENDT_ENV_ALPHA_A_PRIM` | approchée | 140 |
| `G_CC_BLENDT_ENV_ALPHA_A_TxP` | approchée | 148 |
| `G_CC_BLENDPE`, `G_CC_BLENDPE_A_PRIM` | multipasse | 107 |
| `G_CC_BLENDI_ENV_ALPHA*` | multipasse | 99 |
| `G_CC_BLEND_SHADEALPHA` | multipasse | 99 |
| configurations à deux texels | E05-S04 | 60 à 181 |

**Toute configuration déclarée exacte l'est**, à un pas de quantification près.
C'est le seul contrôle que le harnais compte en échec : les catégories
`multipasse` et `approchée` annoncent un écart, et le mesurer est leur raison
d'être.

Les huit unités résiduelles des trois `DECAL*` viennent du chemin de texture,
où le texel traverse le combineur avec un facteur d'un — donc la même troncature
255/256, que le modèle n'applique pas au texel. Systématique, benin, et signalé
plutôt que masqué par une tolérance élargie.

Les écarts de 99 à 148 sont ceux que la traduction en une passe ne peut pas
éviter aujourd'hui. **Aucun seuil d'acceptation n'est encore posé** : l'issue par
l'alpha du sommet n'a pas été éprouvée, et accepter un écart qu'on sait peut-être
évitable serait prématuré.
