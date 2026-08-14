# Le rastériseur de référence, et pourquoi il se mesure au lieu de se regarder

Relevé de [E04-S08](../stories/E04-hle-f3ddkr/E04-S08-rasteriseur-logiciel-reference.md),
14 août 2026.

## Sa raison d'être tient en une phrase

Quand une image sera fausse en Glide, il faudra savoir si l'erreur vient du
décodeur ou du backend. Sans oracle intermédiaire, un pixel faux peut venir de
dix étages — transformation, découpage, décodage de texture, traduction de
combineur, réglage Glide, pilote. Avec un second backend implémentant la **même
interface** (E04-S01), la question se tranche en une exécution.

## Un oracle qu'on vérifie à l'œil n'est pas un oracle

Sa valeur entière tient dans la confiance qu'on lui accorde, et « ça a l'air
juste » ne se transmet pas. Chaque contrôle compare donc un pixel relu à une
valeur **calculée analytiquement**.

Le contrôle central est celui de la correction perspective, parce que c'est
celui qu'un rastériseur naïf rate en silence. Un triangle dont les sommets ont
des `w` très différents — 1 et 4, le cas d'une surface vue en oblique — donne au
milieu de l'arête :

```
1/w = 0,5 × 1 + 0,5 × 0,25       = 0,625
s/w = 0      + 0,5 × 0,25        = 0,125
s   = 0,125 / 0,625              = 0,20
```

Mesuré : **0,2039**. La texture employée est une rampe d'intensité de 256 texels
où le texel `i` vaut `i` ; relire un pixel donne donc directement la coordonnée
qui a servi à l'échantillonner.

### L'auto-test a corrigé mon raisonnement

La première version de ce contrôle disait « ce n'est pas la valeur affine 0,50 ».
En cassant délibérément la correction perspective, l'auto-test a montré que
l'oubli de la division ne donne pas 0,50 mais **0,125** — et que le contrôle
passait donc sur un rastériseur cassé.

Il y a deux façons de se tromper, et elles ne donnent pas la même valeur :

| | `s` obtenu |
|---|---:|
| correct — diviser `s/w` par `1/w` | **0,20** |
| interpoler `s/w` et oublier de diviser | 0,125 |
| ranger `s` au lieu de `s/w` et interpoler en espace écran | 0,50 |

Le contrôle vise 0,20 avec une tolérance serrée et **rejette les deux autres
nommément**. Une tolérance large aurait accepté 0,125.

## L'hôte et Windows 95 rendent le même pixel

Le même code compilé des deux côtés produit des fichiers **identiques, octet pour
octet**. C'est ce qui autorise à comparer une image produite sur l'hôte à une
image produite sur la machine — sans quoi l'oracle ne servirait que là où il
tourne.

26 contrôles, verts sur l'hôte et sur la cible.

## Ce qui est couvert

| | |
|---|---|
| Correction perspective | mesurée contre la valeur analytique |
| Enveloppement | répétition, bornage, miroir, chacun à une coordonnée connue |
| Filtrage | point et bilinéaire, le bilinéaire vérifié sur la moyenne de deux texels voisins |
| Profondeur | et **l'indépendance à l'ordre d'émission**, qu'un tampon de profondeur promet |
| Test alpha | de part et d'autre du seuil |
| Mélange | alpha à moitié sur du noir |
| Fenêtre de ciseaux | coupe à gauche, laisse passer, coupe à droite |
| Sortie en fichier | BMP 24 bits, en-tête et dimensions relus |

Le demi-texel du filtrage bilinéaire mérite une mention : sans lui l'image est
décalée d'une demi-largeur de texel, ce qui ne se voit pas sur une mire et se
voit parfaitement sur une comparaison d'images — exactement le genre d'écart que
ce backend existe pour arbitrer.

## Ce qui n'est pas fait, et pourquoi

- **Le combineur du RDP dans sa forme complète.** Les quatre modes de l'interface
  sont implémentés fidèlement, sans contrainte de matériel ; mais le combineur du
  RDP a deux étages à quatre entrées, et l'inventaire de ceux que DKR emploie
  réellement est le travail de **E04-S06**, encore `TODO`.
- **Une image du jeu.** Le critère « écran-titre, menu, une course » demande que
  le jeu tourne, donc la ROM. Le rastériseur est prêt à la recevoir.

Il a le droit d'être lent, et pas celui d'être compliqué : un rastériseur
optimisé est un rastériseur dont il faut à son tour vérifier la justesse, et
l'oracle disparaît. Flottants partout, pas de tuiles, pas de table précalculée,
une boucle par pixel de la boîte englobante.
