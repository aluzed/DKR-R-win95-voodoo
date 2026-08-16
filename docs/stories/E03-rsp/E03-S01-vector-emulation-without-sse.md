# E03-S01 — Émulation de l'unité vectorielle du RSP sans SSE

| | |
|---|---|
| **Épic** | E03 — RSP sur x86 sans SSE |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E00-S04, E01-S05 |
| **Bloque** | E03-S02 |

## État au 2026-08-11 — largement caduc

[E00-S04](../E00-scoping/E00-S04-spike-rsp-cost-without-sse.md) a mesuré ce que ce
ticket devait rendre possible, et le résultat lui retire l'essentiel de sa
raison d'être :

- **le repli scalaire existe déjà** dans `librecomp` et se sélectionne
  automatiquement en 32 bits — les étapes 1 à 3 sont sans objet ;
- **l'optimisation MMX ne sauverait pas le chemin** : quatre voies au lieu de
  huit amèneraient à ~15 % du débit du RSP, cinq fois trop lent. Les étapes 4 à 7
  seraient un effort perdu.

Ce qui subsiste : le chemin scalaire est l'**oracle** de
[E03-S03](E03-S03-high-level-mixer-fallback.md), et il est déjà fonctionnel. Ce
ticket devrait être fermé ou réduit à la validation de cet oracle.

## Contexte

`RecompiledRSP/aspMain.cpp` inclut `librecomp/rsp_vu_impl.hpp`, qui émule
l'unité vectorielle du RSP. Cette unité traite huit entiers signés de 16 bits par
instruction, avec accumulateur 48 bits et saturations — une correspondance
naturelle avec SSE2, dont les registres font 128 bits, soit exactement huit
voies de 16 bits.

Le Pentium II n'a pas SSE2. Il a MMX : registres de 64 bits, soit quatre voies de
16 bits. Chaque opération vectorielle du RSP demandera donc deux opérations MMX,
plus la gestion des retenues et des saturations sur les deux moitiés.

Deux particularités de MMX pèsent sur la conception :

- ses registres sont **partagés avec la pile x87**. Toute transition entre code
  MMX et code flottant exige un `emms`, dont le coût est significatif. Le
  découpage doit donc grouper le travail vectoriel plutôt que l'entrelacer ;
- l'accumulateur 48 bits du RSP n'a pas d'équivalent MMX, et sa reproduction
  exacte est la partie délicate de l'exercice.

E00-S04 a mesuré ce que coûte chaque stratégie. Ce ticket implémente celle qui a
été retenue.

## Objectif

Fournir une implémentation de l'unité vectorielle du RSP qui compile sans SSE,
produit des résultats identiques au bit près à la référence, et tient le budget
de E00-S04.

## Périmètre

**Dans :** l'émulation vectorielle et sa validation.

**Hors :** le microcode audio lui-même (E03-S02) et la sortie audio (E06-S03).

## Travail

1. Écrire d'abord l'implémentation **scalaire portable**, sans MMX. Elle sera
   lente, et c'est précisément son intérêt : elle est simple, évidemment correcte,
   et elle sert d'oracle à la version optimisée. Elle est aussi le repli si MMX
   pose problème.
2. Valider cette version scalaire contre la version SSE2 de référence : rejouer
   des tâches audio capturées et comparer les tampons de sortie au bit près. Une
   différence, même d'une unité de quantification, est un défaut.
3. Écrire les tests par opération : pour chaque instruction vectorielle utilisée
   par `aspMain` (relevées et comptées en E00-S04), un test couvrant les cas
   nominaux, les saturations positives et négatives, et les débordements
   d'accumulateur. C'est aux frontières que ces implémentations se trompent.
4. Optimiser en MMX les opérations dominantes relevées en E00-S04 — pas toutes.
   Les opérations rares restent scalaires : leur optimisation coûte du temps et du
   risque pour un gain non mesurable.
5. Gérer les transitions MMX / x87 : placer les `emms` aux bonnes frontières,
   vérifier qu'aucun code flottant ne s'exécute avec un état MMX actif. C'est une
   source classique de corruption silencieuse de résultats en virgule flottante.
6. Revalider la version MMX contre la version scalaire, au bit près, sur le même
   jeu de tâches capturées.
7. Mesurer le gain réel et le confronter au budget de E00-S04.
8. Livrer le tout sous forme de patch `patches/n64-modern-runtime/`, jamais par
   modification directe du worktree.

## Critères d'acceptation

- [ ] L'implémentation scalaire produit une sortie identique au bit près à la
      référence SSE2, sur au moins dix tâches audio capturées distinctes.
- [ ] Chaque opération vectorielle utilisée par `aspMain` a un test couvrant
      nominal, saturations et débordement d'accumulateur.
- [ ] L'implémentation MMX produit une sortie identique au bit près à la version
      scalaire.
- [ ] Les transitions MMX / x87 sont traitées, et un test le vérifie en
      entrelaçant volontairement du code flottant.
- [ ] Le coût est mesuré et confronté au budget de E00-S04.
- [ ] La livraison est un patch sous `patches/`, et la cible moderne continue
      d'utiliser le chemin SSE2 sans régression.
- [ ] Le choix des opérations optimisées est justifié par la distribution mesurée.

## Risques

L'écriture de SIMD à la main est le terrain le plus propice aux erreurs
silencieuses : un décalage de saturation produit un son légèrement faux, que
personne ne remarque avant longtemps. La discipline de comparaison au bit près
contre une implémentation scalaire simple est non négociable.

## Références

- `runtime-recomp/RecompiledRSP/aspMain.cpp:1-2`
- `librecomp/include/librecomp/rsp_vu_impl.hpp` (dans le worktree préparé)
- E00-S04 — distribution des opérations et budget
