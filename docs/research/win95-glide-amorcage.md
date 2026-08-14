# Le premier pixel Glide passe par la couche du moteur

Relevé de [E05-S01](../stories/E05-glide/E05-S01-initialisation-glide-buffers.md),
14 août 2026, sur la machine de test — Voodoo 2 à 2 Mo de tampon d'images et
2 Mo par TMU, la **configuration plancher** de l'ADR 0002.

```text
detection          : succes
version Glide      : 0x254
cartes             : 1
TMU                : 2
memoire image      : 2048 Ko
  TMU 0 memoire    : 2048 Ko
  TMU 1 memoire    : 2048 Ko
deux TMU exigees   : OUI
ouverture 640x480  : succes
resolution obtenue : 640x480, 2 tampons, profondeur oui
100 images en      : 1573 ms
cadence            : 63 images/s
fermeture          : affichage restitue
seconde fermeture  : sans effet, comme attendu
```

E09-S01 avait déjà dessiné un triangle. Ce qui change ici est que le chemin
emprunté est **celui que le moteur utilisera** — `platform/render/glide.c` — et
non une démonstration jetable. Ce qui est prouvé est donc ce qui servira.

## Ce que la cadence dit, et ce qu'elle ne dit pas

63 images par seconde sur un effacement, un triangle et un échange. L'échange
est synchronisé sur le balayage, et la mesure montre simplement que **le cycle
d'image n'est pas le goulot** à charge triviale.

Elle ne dit **rien** du taux de remplissage du jeu réel, qui est la question de
E08. Un triangle n'est pas une scène.

## L'affichage est restitué, y compris après un plantage

C'est la propriété qui compte le plus en pratique, et elle est mesurée dans les
deux sens.

Sur une Voodoo 1 ou 2, la carte prend la main par un relais analogique : tant
que le contexte est ouvert, l'écran affiche la sortie 3dfx et non celle de la
carte 2D. Une fermeture manquée laisse donc un écran noir que seul un
redémarrage récupère — pendant toute la mise au point de E05, où l'on plante
souvent, c'est la différence entre dix secondes et deux minutes par erreur.

Le mode « crash » du témoin ouvre le contexte puis déréférence un pointeur nul.
La chaîne éprouvée est complète :

1. `dkr_win95_startup` pose le filtre d'exception (E02-S03) ;
2. `dkr_glide_open` inscrit la restitution au registre d'arrêt anormal ;
3. le filtre exécute le registre **avant** d'afficher quoi que ce soit.

Résultat sur la machine : le bureau Windows réapparaît, puis la boîte du filtre.
Pas d'écran noir.

## Deux pièges, dont un que seule la machine pouvait dire

### Une détection qui a des effets de bord n'est pas une détection

La première version déchargeait et rechargeait `glide2x.dll` à chaque appel de
`dkr_glide_detect`. Le témoin appelait `detect` puis `open`, qui redétectait : le
second `grGlideInit` tombait sur une bibliothèque tenant encore la carte, et
Glide refusait par

```text
Mutual exclusion prohibits this
```

— un message qui ne désigne pas sa cause. La détection est désormais idempotente
et rend ce qu'elle sait déjà.

### `grSstQueryHardware` ne distingue pas les générations

Relevé en E00-S05 et confirmé ici : le type rendu vaut `0` pour une Voodoo 2
comme pour une Voodoo 1, et la révision FBI est identique. La détection repose
donc sur le **nombre de TMU et la mémoire par TMU** — qui sont d'ailleurs les
deux seules choses dont le moteur ait besoin pour trancher entre une passe et
deux. Voir [`win95-voodoo2-machine.md`](win95-voodoo2-machine.md).

## Ce qui n'est pas fait

E05-S01 n'est **pas** terminé, et deux de ses dépendances sont encore `TODO` :

- **E04-S01**, l'interface de backend que cette couche devra implémenter. Elle
  n'existe pas ; `glide.h` expose donc une API étroite qui lui préexiste, et qui
  s'y adaptera sans que la mise en œuvre change.
- **E04-S05**, dont vient la fenêtre de ciseaux à traduire vers `grClipWindow`.

Restent aussi non éprouvés, faute de pouvoir les provoquer sans casser la
machine : le repli de résolution — la mémoire suffisait — et le message rendu
quand la carte ou la bibliothèque est absente. Les deux chemins existent et
rendent un texte nommant le geste possible ; aucun n'a été exercé.
