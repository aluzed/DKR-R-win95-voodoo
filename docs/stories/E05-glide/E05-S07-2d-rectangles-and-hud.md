# E05-S07 — Rectangles 2D, HUD et interface du jeu

| | |
|---|---|
| **Épic** | E05 — Backend Glide |
| **Statut** | IN_PROGRESS |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E05-S01, E05-S02, E04-S07 |
| **Bloque** | E09-S02 |

## Contexte

Une grande partie de ce que le joueur voit n'est pas de la 3D : les menus, la
carte de sélection de niveau, le compteur de position, le chronomètre, les icônes
de ballons, les écrans de dialogue, les cinématiques. Tout cela passe par des
rectangles texturés et des rectangles pleins.

La N64 les dessine par des commandes RDP dédiées — `FillRect` figure parmi les
gestionnaires du décodeur actuel — qui s'expriment directement en coordonnées
écran, sans transformation. Glide n'a pas de primitive de rectangle : tout se
dessine en triangles. La conversion est simple, mais elle recèle deux pièges
classiques :

- **le décalage d'un demi-texel**, qui rend les éléments d'interface flous ou
  décalés d'un pixel — défaut discret, permanent, et très visible sur du texte ;
- **la règle de remplissage**, qui décide si le bord droit et le bord bas d'un
  rectangle sont inclus. Une erreur y produit des jointures d'un pixel entre
  éléments adjacents, particulièrement visibles sur les fonds composés de tuiles.

L'interface est aussi ce que le joueur regarde le plus longtemps, et ce sur quoi
un défaut se remarque immédiatement.

## Objectif

Rendre correctement tous les éléments 2D du jeu, au pixel près.

## Périmètre

**Dans :** rectangles pleins et texturés, éléments d'interface, positionnement au
pixel.

**Hors :** l'interface de configuration du portage (E06-S05), qui n'est pas du
rendu de jeu.

## Travail

1. Traduire les commandes de rectangle en paires de triangles, en coordonnées
   écran, sans passer par le pipeline de transformation.
2. Régler le décalage de demi-texel. Il se détermine par l'expérience — dessiner
   une grille de un pixel sur un fond contrasté et vérifier son alignement — pas
   par le raisonnement.
3. Vérifier la règle de remplissage sur des rectangles adjacents : aucune jointure,
   aucun recouvrement.
4. Traiter les rectangles à coordonnées de texture, avec leur mode
   d'échantillonnage propre. Le gestionnaire `TextureOffset` du décodeur indique
   que le microcode a sa propre gestion des décalages : la relever plutôt que la
   supposer.
5. Traiter les rectangles pleins, sans texture.
6. Vérifier le rendu du texte du jeu, qui est composé de petites textures assemblées
   et constitue le test le plus exigeant du positionnement au pixel.
7. Comparer au pixel près avec la référence sur des écrans figés : écran-titre,
   menu principal, sélection de personnage, HUD en course, écran de résultats.
   L'interface étant statique, la comparaison peut y être exacte plutôt que
   tolérante — c'est une occasion rare dans ce projet et il faut la saisir.
8. Vérifier le HUD en écran partagé, où les rectangles sont contraints par la
   fenêtre de ciseaux (E04-S05).

## Critères d'acceptation

- [x] Les rectangles texturés et pleins sont rendus, en coordonnées écran et
      sans passer par le pipeline de transformation.
- [x] Vérifié sur une grille d'un texel, et le résultat est **qu'aucune
      correction n'est nécessaire** : le décalage nul aligne les 64 colonnes. La
      bande sûre va de −0,5 à +0,25 texel, la rupture tombant exactement là où le
      point échantillonné change de texel. Savoir qu'il reste un quart de texel
      de marge de chaque côté dit qu'une petite erreur d'arrondi ailleurs dans la
      chaîne ne fera pas basculer l'interface.
- [x] Quatre rectangles bord à bord : 0 pixel de fond sur 200. Avec un contrôle
      négatif vérifiant que les quatre couleurs sont distinctes, sans quoi un
      seul rectangle couvrant tout passerait.
- [ ] Le texte du jeu — **bloqué par la ROM**. C'est l'épreuve la plus exigeante
      du positionnement, et la grille d'un texel n'en est qu'un substitut.
- [ ] Cinq écrans de référence — **bloqué par la ROM**.
- [x] Exact au pixel près dans les quatre cas mesurés : 153600 peints pour
      153600 attendus à deux joueurs, 76800 pour 76800 à quatre.
- [x] Relevé — **et il a révélé une erreur du décodeur**. `w1` est une adresse
      RDRAM, base de chargement de texture, et la commande remet à zéro le
      décalage et le compte. Notre décodeur la lisait comme deux décalages `s` et
      `t` sur seize bits. L'erreur aurait déplacé des motifs plutôt que de les
      faire disparaître, et l'on aurait cherché du côté du décodage de texture.

> **Correction du 15 août 2026** : ce critère avait été marqué bloqué par l'absence de ROM. La ROM était présente — voir `docs/research/win95-rom-available.md`. Le blocage n'existe plus ; ce qui reste à faire l'est pour d'autres raisons, ou n'a simplement pas encore été fait.

## Risques

Un décalage d'un demi-texel est le genre de défaut qu'on cesse de voir après
quelques heures d'exposition, et qui saute aux yeux de toute personne découvrant
le portage. La comparaison exacte de l'étape 7 est ce qui empêche de s'y habituer.

## Références

- `runtime-recomp/src/game/f3ddkr_rt64.hpp` — gestionnaires `FillRect`,
  `TextureOffset`
- E04-S07 — décodage des textures d'interface
- E09-S02 — harnais de comparaison
