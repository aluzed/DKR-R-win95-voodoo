# Le microcode F3DDKR, commande par commande

Cartographie établie en lisant `runtime-recomp/src/game/f3ddkr_rt64.cpp`, le
décodeur qui tourne aujourd'hui. Elle a une valeur propre : c'est la description
du microcode de Rare, indépendante de ce portage et du moteur de rendu employé.

Toutes les commandes font **deux mots de 32 bits**, `w0` puis `w1`, `w0` portant
l'opcode dans son octet de poids fort.

## Les treize opcodes

| Opcode | Nom | Rôle |
|---:|---|---|
| `0x01` | `Matrix` | charge une matrice depuis RDRAM, sélectionne un emplacement |
| `0x02` | `TextureOffset` | décalage appliqué aux coordonnées de texture |
| `0x03` | `MoveMem` | écriture d'un bloc de mémoire du RSP |
| `0x04` | `Vertex` | charge des sommets dans le cache de 32 entrées |
| `0x05` | `Triangle` | dessine des triangles indexés sur ce cache |
| `0x06` | `DisplayListBranch` | branchement ou appel d'une liste imbriquée |
| `0x07` | `CountedDisplayList` | liste dont le nombre de commandes est donné |
| `0xB8` | `EndDisplayList` | retour de liste, ou fin |
| `0xBC` | `MoveWord` | écriture d'un mot d'état |
| `0xBF` | `DMAOffsets` | **bases d'adressage des matrices et des sommets** |
| `0xF3` | `LoadBlock` | charge un bloc de texels |
| `0xF6` | `FillRect` | rectangle plein |
| `0xFD` | `SetTextureImage` | adresse, format et taille de l'image de texture |

Un quatorzième gestionnaire, `PresentationGroup`, n'a pas d'opcode : il est
atteint par `MoveWord` avec le type `0xFE` et un mot magique — voir plus bas.

## `DMAOffsets` (0xBF) — le mécanisme central

```
w0 : base des matrices   (masquée par 0x00FFFFFF)
w1 : base des sommets    (masquée par 0x00FFFFFF)
```

C'est **la spécificité du microcode de Rare**, et le point où une erreur ne
pardonne pas : matrices et sommets ne sont pas adressés absolument mais par des
décalages relatifs à ces deux bases. Une base fausse ne produit pas un plantage
mais une géométrie entièrement absurde, ce qui est bien plus difficile à
diagnostiquer.

## `Vertex` (0x04)

```
w0  bit 16      : ajouter au lot courant plutôt que le remplacer
    bits 19..23 : nombre de sommets, moins un
    bits  9..13 : index de destination dans le cache
w1              : adresse, relative à la base des sommets
```

Chaque sommet occupe **dix octets** : `x`, `y`, `z` en entiers 16 bits signés,
puis `r`, `g`, `b`, `a` en octets. **Il ne porte pas de coordonnées de texture**
— celles-ci arrivent par coin au moment du triangle, ce qui décide de la forme de
l'interface de rendu (E04-S01).

Le cache compte **32 entrées**.

## `Triangle` (0x05)

```
w0  bits 16..19 : état de texture (activation)
    bits 20..23 : nombre de triangles, moins un
w1              : adresse de la table de triangles
```

Chaque triangle occupe **seize octets** :

| Décalage | Contenu |
|---:|---|
| 0 | drapeaux — le bit `0x40` désactive la culling |
| 1, 2, 3 | les trois index dans le cache de sommets |
| 4, 6 | `s`, `t` du premier coin, en 16 bits signés |
| 8, 10 | `s`, `t` du deuxième coin |
| 12, 14 | `s`, `t` du troisième coin |

Le sens de culling dépend du **signe de l'échelle en x de la fenêtre
d'affichage** : positif, on élimine les faces arrière ; négatif, les faces avant.

## `Matrix` (0x01)

```
w0  bits  0..15 : doit valoir 64 — sinon la commande est ignorée
    bits 16..19 : emplacement, ou 0
    bits 22..23 : emplacement de repli quand le précédent vaut 0
w1              : adresse, relative à la base des matrices
```

Trois emplacements au plus ; l'index est borné à 2. La matrice fait 64 octets.

## `MoveWord` (0xBC)

```
w0  bits 0..7 : type
w1            : valeur
```

| Type | Effet |
|---:|---|
| `0x02` | mode panneau d'affichage — le bit 0 de `w1` |
| `0x0A` | sélection de matrice — bits 6..7 de `w1`, borné à 2 |
| `0xFE` | groupe de présentation, si `w1 & ~0xFF` vaut `0x444B5200` |

Le groupe de présentation est une **extension du portage**, pas du microcode
d'origine : le mot magique `'DKR\0'` distingue les commandes ajoutées par le
moteur moderne de celles du jeu. Ses modes sont l'ombre (2), la pièce de
véhicule (4), le panneau d'affichage (6) et la surface (7).

## Contrôle de flux

`DisplayListBranch` (0x06) branche ou appelle selon un drapeau ; l'adresse cible
est masquée par `0x00FFFFF8` — **alignée sur huit octets**, la taille d'une
commande. `EndDisplayList` (0xB8) dépile. `CountedDisplayList` (0x07) porte son
nombre de commandes dans les bits 16..23 de `w0`.

La pile de retour compte **32 entrées**.

## La validation des plages, et pourquoi elle survit à l'extraction

Le décodeur actuel **valide toutes les plages avant de les utiliser**, et rejette
les données invalides par une erreur bornée plutôt que de laisser adresser la
mémoire hôte.

| Commande | Ce qui est vérifié |
|---|---|
| `Matrix` | adresse ≤ 8 Mio − 64 |
| `Vertex` | nombre ≤ 32, destination + nombre ≤ 32, fin ≤ 8 Mio |
| `Triangle` | nombre ≠ 0, fin ≤ 8 Mio, **chaque index < 32** |
| `DisplayListBranch` | cible ≤ 8 Mio − 8, profondeur de pile < 32 |
| `CountedDisplayList` | nombre ≠ 0, adresse ≠ 0, fin ≤ 8 Mio |
| `LoadBlock` | adresse dans RDRAM |

RDRAM fait **8 Mio** (`0x00800000`) et les adresses sont masquées par
`0x00FFFFFF`.

Cette discipline protège contre une ROM modifiée comme contre un bug du portage.
Le rejet est **circonscrit** : il interrompt la commande, pas le jeu — et il est
journalisé, avec un compteur qui borne le volume, parce qu'une display list
corrompue produirait sinon des milliers de lignes par image.

Un détail qui compte pour l'extraction : la validation de `Triangle` **vérifie
tout le lot avant d'en dessiner le premier**. Valider au fil de l'eau
laisserait dessiner des triangles valides avant de rejeter le lot, ce qui rend le
défaut dépendant du contenu et donc difficile à reproduire.
