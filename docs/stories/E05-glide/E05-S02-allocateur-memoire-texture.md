# E05-S02 — Allocateur de mémoire de texture TMU

| | |
|---|---|
| **Épic** | E05 — Backend Glide |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E05-S01, E04-S07 |
| **Bloque** | E05-S04, E05-S08, E08-S01 |

## Contexte

Glide n'a pas de gestionnaire de textures. Il expose la mémoire de la TMU comme un
espace d'adressage brut : l'application choisit une adresse, y télécharge une
texture par `grTexDownloadMipMap`, et lie cette adresse au moment de dessiner.
Tout le reste — allocation, fragmentation, éviction — est à écrire.

C'est le poste où le budget est le plus serré. Une Voodoo 2 offre 2 ou 4 Mo par
TMU selon le modèle, et le pic de working set mesuré côté portage natif est de
**1,20 Mo par niveau** — confortable sur 2 Mo, à condition que le décodage de
E04-S07 n'ait pas développé les textures indexées, auquel cas le chiffre peut
quadrupler et le budget sauter.

Deux contraintes matérielles pèsent sur l'allocateur :

- l'alignement et la granularité imposés par la TMU, qui rendent la fragmentation
  plus coûteuse qu'un simple allocateur ne le laisserait croire ;
- avec deux TMU (E05-S04), une texture destinée au multitexturage doit être
  présente sur la bonne unité, ce qui fait deux espaces à gérer, pas un.

## Objectif

Livrer un allocateur de mémoire de texture qui tienne un niveau entier sans
téléchargement en cours de course.

## Périmètre

**Dans :** allocation, téléchargement, éviction, mesure, et les deux TMU.

**Hors :** le décodage des textures (E04-S07) et le choix des combineurs
(E05-S03).

## Travail

1. Mesurer le comportement réel avant de concevoir : sur un niveau complet, le
   nombre de textures distinctes, leur taille cumulée, et le motif de réutilisation
   d'une image à l'autre. C'est ce motif qui décide de la politique, pas la théorie
   de l'allocation.
2. Implémenter l'allocateur avec la granularité et l'alignement de la TMU. Si les
   textures du jeu se répartissent en un petit nombre de tailles, un allocateur par
   classes de taille éliminera la fragmentation à peu de frais — à vérifier sur les
   chiffres de l'étape 1.
3. Implémenter le téléchargement par `grTexDownloadMipMap` et le suivi de ce qui
   est résident.
4. Implémenter l'éviction. Le moindre récemment utilisé est le point de départ
   raisonnable, mais la mesure de l'étape 1 peut montrer qu'un préchargement
   complet au chargement de niveau suffit — auquel cas il n'y a pas d'éviction du
   tout en cours de course, ce qui est de loin le meilleur résultat.
5. Gérer les deux TMU comme deux espaces distincts, avec la politique de placement
   qu'impose E05-S04.
6. Instrumenter : occupation, taux de succès du cache, nombre et volume des
   téléchargements par image. Ces compteurs doivent être lisibles en jeu (E08-S01),
   parce qu'un défaut de cache de texture se diagnostique en jouant.
7. Traiter la saturation : si un niveau ne tient pas, décider — réduction de
   résolution des textures, ou téléchargement en cours de partie assumé — et le
   documenter plutôt que de laisser l'allocateur échouer.
8. Vérifier sur tous les niveaux du jeu, pas sur un échantillon. C'est une mesure
   automatisable : charger chaque niveau et relever le pic.

## Critères d'acceptation

- [ ] Le nombre de textures, le volume et le motif de réutilisation sont mesurés
      par niveau, sur tous les niveaux.
- [ ] L'allocateur respecte l'alignement et la granularité de la TMU.
- [ ] Aucun téléchargement de texture en cours de course sur les niveaux qui
      tiennent en mémoire — vérifié par compteur, pas par observation.
- [ ] La politique d'éviction est justifiée par la mesure de l'étape 1.
- [ ] Les deux TMU sont gérées séparément.
- [ ] Les compteurs d'occupation et de téléchargement sont lisibles en jeu.
- [ ] Les niveaux qui ne tiennent pas sont identifiés, et leur traitement est
      documenté.

## Risques

Un téléchargement de texture en cours de course est un à-coup visible : le bus PCI
de 1998 n'est pas rapide, et transférer une texture de 64 Ko pendant une image de
33 ms se sent. Si la mesure montre qu'un niveau ne tient pas, mieux vaut réduire
la résolution de ses textures au chargement que subir des transferts pendant la
partie.

## Références

- `../../Diddy-Kong-Racing/docs/research/level-working-set.md` — pic de 1,20 Mo
- E00-S05 — mémoire disponible par TMU sur la cible
- E04-S07 — format et taille des textures décodées
