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

## Relire le tampon d'image — la première image réellement vue

Jusqu'ici, tout ce que ce document affirmait sur le rendu Glide reposait sur
**l'absence de plantage**. Sur une Voodoo passthrough, l'écran appartient à la
carte : le relais analogique coupe la sortie 2D tant que le contexte est ouvert,
et aucune capture de l'émulateur ne montre la sortie 3dfx. On savait que
`grDrawTriangle` rendait la main ; on ne savait pas qu'il peignait.

`grLfbLock` / `grLfbUnlock` lèvent cette cécité. `dkr_glide_read_framebuffer`
verrouille le tampon avant, lit du 565, et rend de l'ARGB 32 bits dans l'ordre
du rastériseur logiciel — pour que les deux images se comparent sans conversion.

### La conversion 565 réplique les bits de poids fort

Un simple décalage à gauche donnerait 0xF8 pour le rouge maximal : le blanc
serait gris, et **toute comparaison avec l'oracle dériverait d'un écart constant**
qu'on attribuerait au rastériseur. Répliquer donne bien 255.

Relevé sur la machine, au sommet rouge du triangle de test : `(247, 0, 0)`. Ce
n'est pas 255, et c'est correct : 247 vaut 30 en 5 bits, pas 31 — le point
échantillonné est huit lignes sous le sommet, déjà dans le dégradé. Un 240 aurait
signalé la troncature ; un 247 signale une interpolation.

### Ce que la carte a dessiné

    resolution         : 640x480, 2 tampons, profondeur oui
    cadence            : 64 images/s
    pixels peints      : 75264 sur 307200
    centre de l'ecran  : 0x7B3C39

Les sommets du triangle sont à (320,72), (544,408) et (96,408). Son aire
analytique vaut ½ × 448 × 336 = **75264 pixels — exactement le compte relevé**.
La carte remplit donc la surface géométrique sans débordement ni manque, et sa
règle de remplissage des bords ne compte chaque pixel qu'une fois.

Le centre `0x7B3C39` est rouge-dominant, ce qui est la bonne réponse et non la
réponse évidente : le centre de l'*écran* (320,240) n'est pas le centre de
gravité du triangle (320,296), il est plus proche du sommet rouge. Un mélange à
parts égales aurait au contraire trahi une interpolation fausse.

### Un piège dans le témoin lui-même

La première version comptait « pixels peints » après la boucle de cadence, qui
efface sur un dégradé : les 307200 pixels étaient peints et le verdict était
positif sans rien prouver. Le témoin dessine désormais une dernière image **sur
fond noir** avant de lire. La leçon vaut au-delà de Glide : un compteur dont la
valeur de fond n'est pas distinguable du résultat ne mesure rien.

L'image complète est écrite en BMP 24 bits (`D:\GLIDEBK.BMP`), lisible depuis
l'hôte — c'est elle qui servira d'entrée à la comparaison de E09-S02.
