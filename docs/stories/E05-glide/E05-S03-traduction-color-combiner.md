# E05-S03 — Traduction du combineur de couleurs N64 vers Glide

| | |
|---|---|
| **Épic** | E05 — Backend Glide |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | XL |
| **Dépend de** | E04-S06, E05-S01 |
| **Bloque** | E05-S04, E09-S02 |

## Contexte

C'est le ticket le plus difficile du projet.

Le combineur du RDP est une unité **programmable** : pour chaque terme — couleur
et alpha — il choisit quatre entrées parmi seize sources possibles, et calcule
`(a - b) × c + d`, sur un ou deux cycles. L'espace des configurations se compte en
milliers.

Le combineur de Glide est **fixe**. `grColorCombine` et `grAlphaCombine` offrent
une liste close de fonctions et de facteurs, et `grTexCombine` en offre une autre
pour l'étage de texture. Ce qui n'entre pas dans cette liste doit être obtenu
autrement : par plusieurs passes de rendu avec mélange, par la seconde TMU, ou par
une approximation.

Ce qui rend le problème traitable, c'est qu'il ne s'agit pas de traduire le
combineur en général. DKR n'utilise que **33 configurations**, inventoriées par le
portage natif voisin, dont **3 seulement lisent deux texels**
(`../../Diddy-Kong-Racing/docs/research/combiner-inventory.md`) — chiffre à
revérifier par E04-S06 sur ce portage. Trente-trois cas concrets, énumérés, dont
on connaît la fréquence et la surface d'écran : c'est un problème fini.

## Objectif

Réaliser chacune des configurations de combineur utilisées par DKR, avec un écart
visuel mesuré et jugé acceptable pour chacune.

## Périmètre

**Dans :** la correspondance configuration RDP → réglage Glide, le multipasse, et
la mesure de l'écart.

**Hors :** le multitexturage sur deux TMU (E05-S04) et le brouillard (E05-S06).

## Travail

1. Partir de l'inventaire de E04-S06, trié par surface d'écran couverte. Traiter
   dans cet ordre : la configuration qui couvre le plus de pixels est celle dont
   l'erreur se verra le plus.
2. Classer chaque configuration en trois catégories :
   - **exacte** — un réglage Glide produit le même résultat ;
   - **multipasse** — plusieurs passes avec mélange y parviennent, au prix du
     budget de remplissage ;
   - **approchée** — aucune combinaison n'y parvient, et l'écart doit être mesuré
     puis accepté ou refusé.
3. Écrire la table de correspondance sous forme de données, indexée par la forme
   canonique de E04-S06 : une recherche, pas une cascade de conditions. Cela rend
   la table lisible, testable, et complétable sans toucher au code.
4. Pour chaque configuration, mesurer l'écart par rapport au rastériseur logiciel
   de référence (E04-S08), qui implémente le combineur fidèlement. La mesure se
   fait par différence d'image, pas à l'œil.
5. Traiter le double cycle, qui correspond à deux étages de combinaison. Selon la
   configuration, il se résout par la seconde TMU (E05-S04), par une seconde
   passe, ou par simplification quand le second étage est neutre.
6. Mesurer le coût du multipasse. Chaque passe supplémentaire double le
   remplissage de la surface concernée, et le remplissage est précisément ce qui
   limite une Voodoo 2 en 640 × 480. Une configuration multipasse couvrant un grand
   nombre de pixels doit être reconsidérée en approchée.
7. Documenter le résultat dans `docs/research/combiner-mapping.md` : par
   configuration, la catégorie, le réglage Glide, l'écart mesuré, et le coût.
8. Journaliser à l'exécution toute configuration absente de la table, avec un
   rendu de repli visible mais non aberrant.

## Critères d'acceptation

- [ ] Toutes les configurations de l'inventaire sont traitées et classées.
- [ ] La table de correspondance est une structure de données indexée par forme
      canonique.
- [ ] L'écart de chaque configuration au rastériseur de référence est mesuré par
      différence d'image.
- [ ] Le double cycle est traité, chaque cas indiquant sa stratégie.
- [ ] Le coût de remplissage du multipasse est mesuré, et les configurations
      multipasse à forte couverture sont réexaminées.
- [ ] `docs/research/combiner-mapping.md` documente catégorie, réglage, écart et
      coût par configuration.
- [ ] Une configuration inconnue est journalisée et rendue par un repli non
      aberrant.
- [ ] Aucune configuration « approchée » ne dépasse un écart écrit et accepté.

## Risques

C'est le ticket qui peut déraper. La tentation sera de traiter les 33
configurations une par une jusqu'à ce que « ça ressemble », sans mesure. L'écart
visuel se cumule alors silencieusement, et le rendu final est diffusément faux
sans qu'aucune erreur ne soit imputable.

La discipline de mesure contre E04-S08 n'est pas une formalité : c'est ce qui
transforme ce ticket d'un travail d'appréciation en un travail vérifiable. C'est
aussi pourquoi E04-S08 est un prérequis et non un confort.

## Références

- `../../Diddy-Kong-Racing/docs/research/combiner-inventory.md` — 33 configurations
- E04-S06 — inventaire revérifié et forme canonique
- E04-S08 — oracle de comparaison
- [Sources Glide 3dfx](https://sourceforge.net/projects/glide/) —
  `grColorCombine`, `grAlphaCombine`, `grTexCombine`
