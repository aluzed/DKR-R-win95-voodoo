# La chaîne de rendu, assemblée sur une scène synthétique

Relevé du 14 août 2026. Cinq modules s'emboîtent pour la première fois :

```
f3ddkr     lit la display list, valide les plages, tient le cache de 32 sommets
transform  applique la matrice modèle-vue-projection (E04-S03)
clip       découpe au plan proche, élimine les faces arrière (E04-S05)
backend    l'interface de E04-S01
software   le rastériseur de référence, qui écrit l'image (E04-S08)
```

Ce qui est établi n'est **pas** que le rendu soit juste — il faudra le jeu pour
cela — mais que la chaîne est **continue** : une commande écrite en RDRAM
ressort en pixels, et chaque étage passe à son voisin ce que celui-ci attend.

## La scène

Construite à la main dans une fausse RDRAM : c'est ce qui rend l'épreuve possible
sans ROM, et ce qui la rend concluante — on connaît la réponse d'avance, donc on
la vérifie au pixel plutôt qu'à l'œil.

Un carré Gouraud face à la caméra, et **un triangle qui traverse le plan
proche**. Sans ce dernier, la chaîne ne serait éprouvée qu'à moitié : le
découpage ne serait jamais sollicité.

```text
triangles demandes : 3, emis : 4
decoupes en deux : 1, ecartes : 0
```

Quatre émis pour trois lus : le triangle à cheval a bien produit un
quadrilatère, donc deux triangles.

## Un piège du portage graphique, rencontré en chemin

La première version de la scène employait une projection où `w = z` **et**
`z_clip = z`. La division donne alors `z/w = 1,0` pour tout sommet, c'est-à-dire
exactement la valeur à laquelle le tampon de profondeur est effacé. Le test
échoue partout, et **rien n'est peint**.

Le symptôme est celui qu'on redoute : un écran vide alors que **chaque étage se
déclare satisfait** — quatre triangles émis, aucun rejet, aucune erreur. C'est le
compteur `emitted` qui a permis de trancher, en montrant que le problème était en
aval du décodeur. Sans lui, le soupçon aurait porté sur la display list.

C'est la raison d'être de ce compteur, et il vaut d'être conservé.

## L'oracle est exact à travers les plates-formes — sauf là où il ne peut pas l'être

Le même code compilé pour l'hôte et pour Windows 95, sur la même scène :

| | |
|---|---:|
| pixels différents | 1 986 sur 76 800 — **2,6 %** |
| écart maximal par composante | 251 sur 255 |

Un écart de 251 n'est pas un arrondi : ce sont des pixels entièrement d'une autre
couleur. Leur répartition dit d'où ils viennent :

| Zone | Pixels différents |
|---|---:|
| le carré, bien conditionné (`y < 115`) | **0** |
| la bande du triangle découpé (`115 ≤ y < 160`) | **1 970** |
| le reste | 16 |

**Zéro différence là où la géométrie est saine.** Toutes les différences sont
concentrées sur le triangle découpé.

### Pourquoi

Un sommet créé par le découpage sort avec `w` égal à la marge du plan proche —
`0,0001`. Projeté, cela donne :

```text
x = 16 000 160  y = 120  z = -250 000  1/w = 10 000
```

Seize millions de pixels. Les fonctions d'arête du rastériseur soustraient alors
des nombres de cet ordre pour obtenir des résultats de l'ordre de l'unité :
c'est une **annulation catastrophique**, et le résultat dépend entièrement de la
précision des intermédiaires.

Or elle diffère entre les deux cibles : l'hôte calcule en SSE sur 32 bits, la
cible en x87 sur **80 bits** (`-mfpmath=387`). Les mêmes opérations, dans le même
ordre, ne rendent donc pas les mêmes pixels — mais seulement là où l'annulation
mord.

### Le remède, appliqué et mesuré

Plutôt que de choisir une marge au jugé, le découpeur borne désormais les
coordonnées **par construction** : il découpe aussi contre une **bande de garde**,
dans l'espace homogène — quatre plans latéraux à quatre demi-écrans du centre,
traités par la même boucle que le plan proche.

Ce n'est pas le découpage complet aux six plans que E04-S05 écarte à juste titre.
La bande est bien plus large que l'écran, donc presque aucun triangle ne la
traverse, et ceux qui restent entièrement dedans sortent par un **court-circuit**
sans qu'aucune arête ne soit calculée.

| | Pixels différents | Part |
|---|---:|---:|
| plan proche seul | 1 986 | 2,59 % |
| plan proche + bande de garde | **479** | **0,62 %** |

### Ce qui reste, et pourquoi il ne peut pas disparaître comme cela

Les 479 pixels restants sont **tous sur une arête** de l'image de référence —
479 sur 479. Leur écart est grand (jusqu'à 250) parce qu'une arête sépare deux
couleurs très différentes : un décalage d'un seul pixel suffit à échanger du
magenta contre du bleu sombre.

Ce n'est plus une annulation catastrophique mais la divergence ordinaire entre
deux unités de calcul : l'hôte évalue les fonctions d'arête en SSE sur 32 bits,
la cible en x87 sur 80 bits, et la couverture bascule sur les pixels que l'arête
frôle.

**Conséquence pour E09-S02 :** la comparaison doit tolérer un pixel d'écart le
long des arêtes. Ce n'est pas un aveu — c'est la seule forme de comparaison qui
ait un sens entre deux rastériseurs, et elle reste sévère : les 76 321 autres
pixels sont identiques au bit près.

Il existe une autre voie, et elle n'est pas retenue : forcer la précision du x87
à 24 bits de mantisse (`_controlfp`) rendrait la cible identique à l'hôte. Mais
ce réglage vaut pour **tout le processus**, y compris le code recompilé du jeu,
dont les calculs attendent la double précision. Échanger la justesse du jeu
contre la commodité d'une comparaison serait un mauvais marché.

La profondeur, elle, est désormais bornée à `[0,1]` à la projection. Une valeur
hors de cet intervalle n'a pas de sens pour le tampon et gagnait le test partout,
en un motif pointillé qui évoquait un défaut de rastérisation plutôt qu'un défaut
de profondeur. Il a fallu projeter un sommet à la main pour le voir.
