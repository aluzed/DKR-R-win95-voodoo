# Les rectangles 2D et l'interface

Mesuré le 14 août 2026 sur la machine d'épreuve, par
`tools/win95/witnesses/rect_probe.c`.

## `TextureOffset` ne fait pas ce que notre décodeur croyait

Le ticket demandait de « relever plutôt que supposer » le comportement de cette
commande. C'est ce qui a révélé l'erreur.

Notre décodeur lisait `w1` comme **deux décalages de coordonnées** sur seize
bits : `texture_offset_s = (w1 >> 16)`, `texture_offset_t = (w1 & 0xFFFF)`. Le
portage voisin, qui tourne, en fait tout autre chose :

    data.texture_offset = w1 & 0x00FFFFFF;   // une adresse RDRAM
    data.texture_shift  = 0;
    data.texture_count  = 0;

C'est une **base d'adressage pour le chargement de texture**, et la commande
remet à zéro le décalage et le compte.

L'erreur ne se serait pas vue tout de suite. Une base d'adresse lue comme deux
décalages produit des coordonnées absurdes sur les surfaces concernées — donc un
motif déplacé, pas une absence — et l'on aurait cherché du côté du décodage de
texture, qui n'y serait pour rien.

## Le décalage de demi-texel : zéro, et la bande sûre

Le ticket insiste : ce réglage « se détermine par l'expérience, pas par le
raisonnement ». Grille de 64 texels sur 64 pixels, un trait blanc d'un texel
toutes les quatre colonnes, échantillonnage au point :

| Décalage | Colonnes en face sur 64 |
|---|---|
| −0,50 | 64 |
| −0,25 | 64 |
| **0,00** | **64** |
| +0,25 | 64 |
| +0,50 | 33 |
| +0,75 | 33 |
| +1,00 | 33 |

**Aucune correction n'est nécessaire.** Avec `s` allant de 0 à la largeur sur
autant de pixels, l'échantillonnage tombe juste. La rupture est nette entre 0,25
et 0,50, ce qui est exactement l'endroit où le point échantillonné change de
texel : la bande sûre est donc bien centrée sur zéro.

Le résultat compte moins que la bande : savoir qu'il reste un quart de texel de
marge de chaque côté dit qu'une petite erreur d'arrondi ailleurs dans la chaîne
ne fera pas basculer l'interface.

### Une métrique qui ne peut pas échouer ne mesure rien

La première version comptait les colonnes « franches » — ni grises ni
intermédiaires — et rendait 64 sur 64 pour **tous** les décalages. En
échantillonnage au point il n'y a jamais de valeur intermédiaire, seulement des
traits déplacés : la métrique ne pouvait pas échouer, donc ne discriminait rien.

C'est la troisième fois de ce portage qu'un contrôle réussit sans rien établir :
après le compteur de pixels peints sur un fond non noir, et la vérification de
cohérence des TMU sur un écran blanc. Le motif est toujours le même — **le
résultat mesuré n'est pas distinguable de l'absence de résultat**.

Corrigée, la métrique compare à la grille attendue : le texel `x` doit tomber sur
le pixel `x`.

## Les jointures

Quatre rectangles adjacents, bord à bord, de couleurs différentes :

    pixels de fond sur la ligne des quatre rectangles : 0 sur 200

Aucune jointure, aucun recouvrement. Un contrôle négatif vérifie en outre que les
quatre couleurs sont bien distinctes — sans quoi un seul rectangle couvrant tout
passerait le premier contrôle.

C'est cohérent avec la règle de remplissage déjà mesurée en E05-S01 : la fenêtre
de Glide est incluse à gauche et exclue à droite, et ses triangles comptent
chaque pixel une fois.

## Le HUD en écran partagé

Un rectangle plein écran, restreint par la fenêtre de ciseaux de E04-S05 :

| Cas | Peints | Attendu |
|---|---|---|
| 2 joueurs, haut | 153600 | 153600 |
| 2 joueurs, bas | 153600 | 153600 |
| 4 joueurs, haut-gauche | 76800 | 76800 |
| 4 joueurs, bas-droit | 76800 | 76800 |

Exact dans les quatre cas, au pixel près.

## Ce qui reste ouvert

- **Le texte du jeu**, composé de petites textures assemblées, qui est l'épreuve
  la plus exigeante du positionnement. Demande la ROM.
- **Les cinq écrans de référence** comparés au pixel près. Le ticket note à juste
  titre que l'interface étant statique, la comparaison peut y être exacte plutôt
  que tolérante — « une occasion rare dans ce projet ». Demande la ROM.
