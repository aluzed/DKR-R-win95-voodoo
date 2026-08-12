# E04-S04 — Billboarding

| | |
|---|---|
| **Épic** | E04 — HLE F3DDKR indépendant de RT64 |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E04-S03 |
| **Bloque** | E09-S02 |

## Contexte

Le billboarding — l'orientation automatique d'un quadrilatère face à la caméra —
est une fonction que Rare a câblée dans son microcode, et c'est l'une des raisons
d'être de F3DDKR. DKR s'en sert massivement : arbres, buissons, particules,
éléments de décor, et une partie des effets.

Cela en fait un poste visuel à fort impact. Un billboard mal orienté ne produit
pas une petite imprécision : il produit un arbre couché, ou invisible sous un
certain angle. Et comme le jeu en affiche beaucoup, c'est aussi un poste de calcul
mesurable.

`docs/F3DDKR.md` confirme que les billboards font partie des entités auxquelles le
portage moderne attache une identité sémantique, ce qui indique que le décodeur
actuel les traite explicitement — le comportement de référence est donc lisible
dans `f3ddkr_rt64.cpp`.

## Objectif

Reproduire le billboarding du microcode F3DDKR, avec le même résultat visuel que
la cible moderne.

## Périmètre

**Dans :** la détection des primitives orientées et le calcul de leur orientation.

**Hors :** leur rendu (E05) et l'ordre de tri en profondeur (E04-S06).

## Travail

1. Relever le comportement exact dans deux sources indépendantes : le décodeur
   actuel (`f3ddkr_rt64.cpp`) et le microcode côté decomp
   (`extern/dkr-decomp`, `include/f3ddkr.h` documente le billboarding). Un
   désaccord entre les deux est une information à ne pas perdre.
2. Identifier le déclencheur : quel bit d'état ou quelle commande met le microcode
   en mode billboard. C'est le point le plus facile à manquer, et une détection
   trop large orienterait de la géométrie qui ne doit pas l'être.
3. Implémenter le calcul d'orientation. La forme habituelle consiste à annuler la
   rotation de la matrice modèle-vue en n'en gardant que la translation et
   l'échelle, mais le microcode de Rare peut avoir sa propre convention — en
   particulier sur l'axe conservé, un billboard cylindrique tournant autour de la
   verticale n'ayant pas le même comportement qu'un billboard sphérique.
4. Vérifier le cas des billboards imbriqués dans une hiérarchie de matrices : un
   objet orienté attaché à un objet en mouvement.
5. Comparer visuellement à la cible moderne sur des scènes de référence riches en
   billboards, en faisant tourner la caméra sur 360° — c'est la rotation qui
   révèle les erreurs d'axe, pas une capture fixe.
6. Mesurer le coût sur une scène dense, et vérifier qu'il reste proportionné au
   nombre de billboards affichés.

## Critères d'acceptation

- [ ] Le déclencheur du mode billboard est identifié et documenté.
- [ ] Le comportement est recoupé entre le décodeur actuel et le decomp, tout
      désaccord étant consigné.
- [ ] Les billboards restent orientés face à la caméra sur une rotation complète
      de 360°, comparée à la cible moderne.
- [ ] Le cas d'un billboard attaché à un objet en mouvement est traité et testé.
- [ ] Le coût est mesuré sur une scène dense et inscrit au budget de E08-S01.
- [ ] Aucune géométrie non concernée n'est orientée par erreur — vérifié sur une
      scène sans billboard.

## Risques

Une convention d'axe erronée produit un résultat correct de face et faux de côté.
Le test doit donc être une rotation continue, et la comparaison doit se faire à
plusieurs angles, pas sur une capture unique.

## Références

- `docs/F3DDKR.md` — billboards parmi les entités à identité sémantique
- `runtime-recomp/src/game/f3ddkr_rt64.cpp`
- `extern/dkr-decomp` — `include/f3ddkr.h`
