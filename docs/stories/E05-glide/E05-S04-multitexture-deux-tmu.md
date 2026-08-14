# E05-S04 — Multitexturage sur deux TMU

| | |
|---|---|
| **Épic** | E05 — Backend Glide |
| **Statut** | IN_PROGRESS |
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

- [~] Le **poids** des configurations à deux texels est relevé : 34 entrées de
      table sur 214, soit 15 %, et la plus lourde du jeu entier
      (`G_CC_BLENDTEX_PRIM`, 32 entrées) en fait partie. Ce n'est pas
      anecdotique. La **surface d'écran** réelle, elle, demande une mesure à
      l'exécution, donc la ROM.
- [x] Le chaînage fonctionne en une passe sur deux TMU, vérifié par relecture :
      `DECAL`, `OTHER` et `ADD` produisent chacun l'image attendue. Les valeurs
      d'énumération sont **mesurées** — `OTHER` vaut 3 et `ADD` vaut 4, décalées
      d'un cran par rapport à ce qui avait été écrit de mémoire.
- [x] Le repli à une TMU produit une image **strictement identique** : 0 pixel
      différent sur 307200, vérifié par différence et non à l'œil.
- [x] L'allocateur gère les deux espaces : `dkr_texture_desc` porte la TMU
      visée, et `bind_texture` lie sur l'unité où la texture réside — lier une
      adresse de la TMU 1 sur la TMU 0 ne provoque aucune erreur, la TMU 0
      échantillonnant ce qui traîne à cette adresse chez elle. Vérifié : chaque
      unité occupe exactement 8192 octets après un chargement.
      La duplication n'a lieu que dans le repli, où elle est le prix à payer —
      et c'est une raison de plus pour que le repli ne soit pas le défaut.
- [x] Le chemin est choisi à l'exécution par `dkr_glide_backend_tmu_count`, qui
      lit la détection de E05-S01 — et le forçage d'épreuve. Aucune capacité
      codée en dur.
- [~] Mesuré : 1552 ms en une passe contre 1662 ms en deux, sur cent images,
      soit 7 % de surcoût. **Ce chiffre demande une réserve** : à 64 images par
      seconde l'échange est synchronisé sur le balayage et la carte attend, donc
      une passe de plus se glisse dans un temps mort. Sept pour cent est le coût
      du repli sur une scène qui ne sature pas le remplissage, pas le coût du
      multipasse en général. Le mesurer sur une scène représentative demande la
      ROM.
- [x] Les coordonnées des deux unités sont cohérentes, vérifiées sur deux motifs
      complémentaires additionnés : 0 pixel non couvert sur 307200, et chaque
      moitié vient bien d'une unité différente. Ce second contrôle n'est pas
      redondant — la première version du témoin comptait zéro pixel noir sur un
      écran entièrement blanc, et réussissait sans rien établir.

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
