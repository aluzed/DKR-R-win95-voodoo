# E05-S04 — Multitexturage sur deux TMU

| | |
|---|---|
| **Épic** | E05 — Backend Glide |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E05-S02, E05-S03 |
| **Bloque** | E08-S01 |

## Contexte

Trois des configurations de combineur utilisées par DKR lisent **deux texels**
(`../../Diddy-Kong-Racing/docs/research/combiner-inventory.md`). Sur une carte à
une seule TMU, elles imposent deux passes de rendu. Sur une Voodoo 2, qui en a
deux, elles se font en une seule passe : `grTexCombine` chaîne la sortie de la
TMU 1 vers la TMU 0.

Trois configurations sur trente-trois, cela paraît anecdotique. Ça ne l'est pas :
il faut regarder la **surface d'écran** qu'elles couvrent, pas leur nombre. Les
combinaisons à deux texels servent typiquement au mélange de textures de terrain
et aux effets de surface — c'est-à-dire à de grandes étendues de pixels. E04-S06
fournit ce chiffre, et c'est lui qui décide de la priorité de ce ticket.

Ce ticket est aussi ce qui rend le repli sur une TMU acceptable : le rendu doit
rester correct sur Voodoo 1 ou Banshee, en deux passes, même si la cible
recommandée en a deux.

## Objectif

Réaliser les configurations à deux texels en une seule passe sur deux TMU, avec un
repli multipasse correct sur une TMU.

## Périmètre

**Dans :** le chaînage des TMU, le placement des textures, le repli.

**Hors :** la traduction générale du combineur (E05-S03).

## Travail

1. Mesurer la surface d'écran couverte par les configurations à deux texels, à
   partir de l'inventaire de E04-S06. Ce chiffre est le seul juge de la priorité
   de ce ticket.
2. Implémenter le chaînage : la TMU 1 échantillonne, sa sortie devient une entrée
   de la TMU 0, dont la sortie alimente le combineur de couleurs.
3. Étendre l'allocateur de E05-S02 : une texture destinée à la TMU 1 doit y être
   résidente. Deux espaces, donc, et une politique de placement qui évite de
   dupliquer inutilement une texture sur les deux unités — la mémoire est la
   ressource rare.
4. Implémenter le repli à une TMU en deux passes, et vérifier qu'il produit le
   même résultat visuel que la passe unique à deux TMU. Comparer par différence
   d'image, pas à l'œil.
5. Sélectionner le chemin à l'exécution selon le nombre de TMU détecté en E05-S01.
   Aucune capacité codée en dur.
6. Mesurer le gain : temps par image sur une scène représentative, une passe
   contre deux.
7. Vérifier la cohérence des coordonnées de texture entre les deux unités. Chaque
   TMU a son propre jeu de coordonnées dans le vertex Glide, et une erreur ici
   produit un décalage entre les deux couches — visible, et facile à confondre avec
   un problème de combineur.

## Critères d'acceptation

- [ ] La surface d'écran des configurations à deux texels est mesurée.
- [ ] Les configurations concernées se rendent en une passe sur deux TMU.
- [ ] Le repli à une TMU produit une image identique, vérifiée par différence.
- [ ] L'allocateur gère les deux espaces sans duplication inutile.
- [ ] Le chemin est choisi à l'exécution selon la détection matérielle.
- [ ] Le gain en temps par image est mesuré.
- [ ] Les coordonnées de texture des deux unités sont cohérentes, vérifiées sur
      une surface où les deux couches doivent se superposer exactement.

## Risques

Le repli à une TMU est facile à écrire et facile à ne jamais tester, faute de
matériel à une seule TMU sous la main. Il doit être testable par configuration —
un réglage qui force le chemin multipasse même sur une carte à deux TMU — sans
quoi il ne sera vérifié qu'après une remontée d'utilisateur.

## Références

- `../../Diddy-Kong-Racing/docs/research/combiner-inventory.md` — 3 configurations
  à deux texels
- E05-S02 — allocateur, à étendre aux deux espaces
- E05-S03 — table de correspondance
