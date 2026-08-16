# L'oracle confronté au matériel

Mesuré le 14 août 2026 sur la machine d'épreuve, par
`platform/render/tests/test_compare.c` (`COMPARE.EXE`).

## Ce que la confrontation a établi

La même scène synthétique traverse deux fois la chaîne complète — décodeur,
transformation, découpage, backend — d'abord vers le rastériseur de référence,
puis vers la Voodoo 2, dont le tampon d'image est relu.

    triangles emis : logiciel 4, carte 4
    surface peinte : logiciel 94848, carte 94848 (0% d'ecart)
    pixels franchement differents : 0 sur 307200
    pire ecart par canal sur toute l'image : 9

Le pire écart vaut **9 sur 255** : un pas de quantification du rouge (8) plus un
du vert (4), arrondis différemment. 77,45 % des pixels sont strictement
identiques. Il ne subsiste aucune divergence de géométrie, de profondeur ni de
couleur.

Ce n'est pas une preuve que le rendu est *juste* — il faudra le jeu pour cela.
C'est une preuve que deux implémentations indépendantes de la même
spécification tombent d'accord, ce qui est la seule vérification disponible sans
ROM, et celle qui attrape la classe d'erreurs la plus coûteuse : celles où chaque
étage se déclare satisfait en produisant autre chose que ce qu'il annonce.

## Le point n'était pas acquis : la première mesure divergeait sur 24 % de l'image

Trois défauts ont été trouvés, tous **dans l'oracle**, aucun dans la carte. C'est
le sens de l'exercice : le rastériseur de référence est le composant que
personne ne peut vérifier autrement.

### 1. La couleur était interpolée avec correction perspective

Le rastériseur divisait `r`, `g`, `b`, `a` par `w` comme il le fait — à juste
titre — pour `s` et `t`. Ni Glide 2 ni le RDP ne corrigent la couleur :
`GrVertex.r/g/b/a` sont itérés en espace écran.

Sur une surface ordinaire, les deux interpolations diffèrent de quelques unités
et l'erreur reste invisible. Elle a explosé sur le premier triangle **découpé au
plan proche** : le sommet créé y porte un `1/w` énorme qui, pondéré, impose sa
couleur à tout le polygone. L'oracle affichait un aplat magenta là où la carte
produisait un dégradé vert — sur un tiers de l'image.

Un oracle dont la cible est Glide doit itérer comme Glide, faute de quoi il
accuse le matériel d'un écart dont il est lui-même l'auteur.

### 2. La profondeur était triée sur `z`, borné au sommet

Un tampon en z est légitime, et c'était le premier choix. `dkr_clip_project`
borne `z` à [0,1] — il le faut, un sommet créé par le découpage sort avec une
profondeur de l'ordre de −200000.

Mais **borner au sommet déforme le gradient sur toute la primitive** : les deux
extrémités ne sont plus à la même échelle, et l'interpolation ment partout entre
elles. Le défaut reste invisible sur une surface entière et n'apparaît qu'à
l'endroit où une primitive découpée en croise une autre — ici un coin de 3 500
pixels, où l'oracle et la carte désignaient chacun une surface différente comme
étant devant.

`oow` n'a pas ce problème : il vaut `1/w`, il est affine en espace écran, il n'a
jamais besoin d'être borné, et c'est exactement ce que la Voodoo range dans son
tampon. Le rastériseur trie donc désormais sur `−1/w`.

`dkr_render_vertex.z` reste rempli : le RDP, lui, trie bien en z, et le jour où
l'on voudra confronter le portage à l'original plutôt qu'au matériel, c'est cette
valeur qu'il faudra.

### 3. La scène ne testait pas la profondeur qu'elle prétendait tester

Avec `z_clip = 0,5 z` et `w = z`, le rapport `z/w` vaut 0,5 pour *tout* sommet :
le quadrilatère et le triangle découpé se retrouvaient exactement à la même
profondeur. Leur recouvrement produisait un conflit de tri, auquel le rastériseur
répondait par un pointillé et la carte par un bord net — deux réponses également
arbitraires à une question mal posée. La comparaison mesurait cette ambiguïté
plutôt que le rendu.

Un terme constant sur z rend `z/w = 0,5 − 20/z`, qui varie avec la distance.

## Deux épreuves ont dû être corrigées, et c'est normal

Changer la sémantique de profondeur a fait échouer deux vérifications qui
passaient. Aucune des deux n'était une régression :

- la suite du rastériseur posait `oow = 1` partout et rangeait la profondeur dans
  `z` seul. Les fixtures renseignent désormais les deux de façon cohérente ;
- l'épreuve de chaîne cherchait du rouge **au centre de l'écran**. Elle y en
  trouvait tant que la couleur était corrigée en perspective ; le centre est
  maintenant occupé par le polygone découpé, vert et cyan. Elle échantillonne
  désormais le quadrilatère là où il est seul, et vérifie en outre que deux
  points distincts diffèrent — sans quoi un aplat passerait pour un dégradé.

Le second cas mérite d'être retenu : **où l'on échantillonne compte autant que ce
qu'on y cherche**, et un contrôle qui vise une surface doit la viser là où rien
d'autre ne la recouvre.

## Le seuil a été resserré après coup

La première version tolérait 24 par canal. C'était le bon choix pour défricher :
assez large pour ne pas être noyé par la quantification, assez serré pour voir
une divergence de tri.

Une fois les trois défauts corrigés, ce seuil ne peut plus rien attraper — il
passerait sur n'importe quelle régression inférieure à un dixième de l'échelle.
Il est donc doublé d'un contrôle à 16, deux pas de quantification : au-dessus du
bruit mesuré (9) et très en dessous de tout écart qui aurait un sens visuel.

Un seuil qu'on ne resserre pas après avoir mesuré le bruit réel finit par
n'affirmer que sa propre indulgence.

## Ce qui n'est pas encore comparé

Les textures — l'allocateur de TMU est E05-S02 et la traduction du combineur
E05-S03. La comparaison porte aujourd'hui sur la géométrie, la couleur itérée et
la profondeur, c'est-à-dire sur tout ce que les deux backends savent faire.
