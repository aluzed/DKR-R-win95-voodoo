# Le chaînage des deux TMU

Mesuré le 14 août 2026 sur la machine d'épreuve (Voodoo 2, 2 TMU), par
`tools/win95/witnesses/multitex_probe.c`.

## Les valeurs de `grTexCombine`, mesurées — et encore fausses de mémoire

C'est la **troisième fois** de ce portage qu'une valeur d'énumération Glide
écrite de mémoire se révèle fausse, et la troisième fois que seule la mesure le
dit. Les deux précédentes : le sens de comparaison du tampon w, puis toute la
famille `BLENDI` du combineur de couleurs.

Balayage sur deux textures faites pour se distinguer — rouge pur sur la TMU 1,
bleu pur sur la TMU 0 :

| Valeur | Lu | Ce que c'est |
|---|---|---|
| 0 | `000000` | zéro |
| **1** | `0000FF` | **`DECAL`** — la TMU 0 seule |
| 2 | `FFFFFF` | (saturé) |
| **3** | `FF0000` | **`OTHER`** — la TMU 1 seule |
| **4** | `FF00FF` | **`ADD`** — les deux |
| 9 | `000000` | multiplication, ou zéro |

`OTHER` vaut 3 et `ADD` vaut 4 : décalées d'un cran par rapport à ce qui avait
été écrit. Le témoin lui-même en a fait les frais — sa vérification de cohérence
demandait `OTHER` en croyant demander `ADD`, et ne voyait donc qu'une seule
couche.

Des textures qui se ressembleraient auraient rendu ce balayage muet. C'est le
même principe que le damier à quatre couleurs de E05-S02 : **une épreuve doit
distinguer les fautes, pas seulement réussir.**

## Un piège du témoin, qui vaut d'être écrit

La première version laissait l'état de rendu sur `DKR_COMBINE_SHADE`. Le
combineur de couleurs ignore alors la sortie des TMU, et les douze fonctions
rendaient toutes du blanc — un écran uniforme qui ne disait rien.

Pire : la vérification de cohérence des coordonnées **y réussissait**, en
comptant zéro pixel noir sur un écran entièrement blanc. C'est exactement le
défaut relevé lors de la relecture du tampon d'image : *un compteur dont la
valeur de fond ne se distingue pas du résultat ne mesure rien, et il est d'autant
plus dangereux qu'il affiche un succès.* Le contrôle vérifie désormais aussi que
les deux moitiés **diffèrent**.

## La cohérence des coordonnées

Chaque TMU a son propre jeu de coordonnées dans le sommet Glide. Une erreur y
décale les deux couches l'une par rapport à l'autre, ce qui **ressemble à un
défaut de combineur** et se diagnostique très mal.

Vérifié sur deux motifs complémentaires — moitié gauche pleine sur l'un, moitié
droite sur l'autre — additionnés par `ADD` :

    pixels noirs : 0 sur 307200
    quart gauche 0xFF0000, quart droit 0x0000FF

Zéro pixel non couvert, et chaque moitié vient bien d'une unité différente.

## Le repli à une TMU

Le ticket nomme le risque : « facile à écrire et facile à ne jamais tester, faute
de matériel à une seule TMU sous la main ». D'où
`dkr_glide_backend_force_single_tmu`, qui force le chemin multipasse sur une
carte qui en a deux. Sans ce drapeau, le repli ne serait vérifié qu'après une
remontée d'utilisateur — donc sur la machine de quelqu'un d'autre, et sans trace.

Résultat : **0 pixel différent sur 307200**. Le repli produit une image
strictement identique.

## Le gain, et la réserve qu'il faut poser

    100 images en une passe   : 1552 ms
    100 images en deux passes : 1662 ms
    surcout du repli          : 7 %

**Sept pour cent n'est pas le coût du multipasse.** C'est son coût *sur une scène
qui ne sature pas le remplissage*. À 64 images par seconde, l'échange de tampons
est synchronisé sur le balayage, et la carte attend : une passe de plus se glisse
dans ce temps mort sans se voir.

Sur une scène réellement limitée par le remplissage — ce que le jeu sera —
doubler la surface peinte coûtera bien davantage. Le chiffre honnête est donc :
le repli est gratuit tant qu'on n'est pas saturé, et le mesurer sur une scène
représentative demande la ROM.

C'est aussi pourquoi le chaînage reste le chemin par défaut, et le repli une
dégradation : le repli duplique en outre la texture de la seconde couche sur la
TMU 0, et la mémoire de texture est la ressource rare.

## L'allocateur

Les deux unités ont leur mémoire propre — deux espaces, pas un. `dkr_texture_desc`
porte donc la TMU visée, et `bind_texture` lie sur l'unité où la texture réside.

Lier une adresse de la TMU 1 sur la TMU 0 ne provoque aucune erreur : la TMU 0
échantillonne simplement ce qui traîne à cette adresse chez elle, et le décor
porte le motif d'un autre.

Vérifié : après chargement d'une texture sur chaque unité, chacune occupe
exactement 8192 octets.

## L'ordre des appels à `grTexCombine`

Glide veut la TMU la plus haute d'abord : c'est elle qui commence la chaîne.
Programmer la TMU 0 avant la TMU 1 la laisse chaînée sur une unité pas encore
configurée. L'effet n'est pas une erreur mais une image construite à partir de
l'état précédent — donc juste tant qu'on ne change rien, et fausse au premier
changement d'état, ce qui est le pire moment pour s'en apercevoir.
