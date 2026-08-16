# E00-S04 — Spike : coût du RSP recompilé sans SSE

| | |
|---|---|
| **Épic** | E00 — Cadrage, mesures et décisions |
| **Statut** | REVIEW |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E00-S02 |
| **Bloque** | E00-S05, E03-S01, E03-S03 |

## État au 2026-08-11 — mesuré, conclusion tranchée

Résultats complets : [`docs/research/rsp-audio-budget.md`](../../research/rsp-audio-budget.md).

| Grandeur | Mesure |
|---|---|
| Repli scalaire dans `librecomp` | **existe déjà**, retenu automatiquement en 32 bits |
| Jeu d'instructions du chemin SIMD | **SSE4.1** — hors de portée de tout Pentium |
| Compilation du microcode 32 bits sans SSE | ✅ sans modification |
| Opération vectorielle, hôte SIMD → cible SISD | 2,22 ns → **410 ns** (185×) |
| Dont pénalité du scalaire seul | **10,4×** (le reste est la machine, 17,7×) |
| Débit vectoriel de la cible | 2,44 M op/s contre ~62,5 M/s pour le RSP |
| **Ce que la cible atteint du RSP** | **3,9 %** |

**Le microcode audio recompilé ne peut pas tenir le temps réel.** Même en
supposant que l'audio ne consomme que 5 % du RSP sur console, il coûterait 43 ms
par image sur la cible — pour un budget de 33,3 ms. À 20 %, cinq fois le budget.

**Trois questions du ticket sont closes :**

1. Le repli scalaire n'était pas à écrire — `rsp_vu.hpp` le sélectionne dès que
   l'architecture n'est ni x86-64 ni arm64.
2. Le chemin SIMD exige **SSE4.1**, pas SSE2 : il était inaccessible à toute la
   gamme Pentium, et non au seul Pentium II.
3. **Écrire du MMX ne sauverait pas ce chemin.** Quatre voies au lieu de huit,
   sans permutation d'octets ni saturation 32 bits : un gain de 3 à 4× amènerait
   à ~15 % du débit du RSP, toujours cinq fois trop lent. C'est un effort à ne
   pas engager.

**Décision : [E03-S03](../E03-rsp/E03-S03-high-level-mixer-fallback.md) passe de
contingence à chemin critique.** Le chemin scalaire garde une utilité — il
exécute le microcode fidèlement, hors temps réel, ce qui en fait l'**oracle**
naturel pour valider le mixeur de haut niveau.

## Contexte

DKR fait deux usages du RSP, et le projet les traite très différemment :

- le **microcode graphique F3DDKR** n'est pas recompilé : il est interprété à
  haut niveau par `f3ddkr_rt64.cpp`, qui lit la display list et la traduit. Bonne
  nouvelle — c'est le chemin bon marché ;
- le **microcode audio `aspMain`** est bel et bien recompilé, instruction par
  instruction, dans `runtime-recomp/RecompiledRSP/aspMain.cpp` (73 Ko de C++
  généré) et s'exécute sur le CPU hôte.

Le RSP est un processeur vectoriel : ses instructions traitent huit entiers de
16 bits d'un coup. `librecomp/rsp_vu_impl.hpp` les émule très probablement en
SSE2 — absent du Pentium II, qui ne dispose que de MMX.

MMX n'est pas un remplacement direct : ses registres font 64 bits, soit quatre
voies de 16 bits. Chaque opération vectorielle du RSP en demandera deux. Et MMX
partage ses registres avec la pile x87, ce qui impose un `emms` à chaque
transition vers du code flottant — le coût réel dépend donc autant de
l'entrelacement que des instructions elles-mêmes.

## Objectif

Chiffrer le coût du microcode audio recompilé sur la cible, et décider si le
chemin « microcode recompilé » tient dans le budget ou s'il faut lui préférer un
mixeur audio de haut niveau.

## Périmètre

**Dans :** `aspMain`, `rsp_vu_impl.hpp`, et le coût comparé des trois stratégies
d'émulation vectorielle.

**Hors :** l'écriture de l'implémentation retenue (c'est E03-S01) et la sortie
audio (E06-S03).

## Travail

1. Lire `librecomp/include/librecomp/rsp_vu_impl.hpp` et relever précisément quel
   jeu d'instructions il exige, et si un repli scalaire portable existe déjà.
2. Compter, dans `RecompiledRSP/aspMain.cpp`, les appels aux opérations
   vectorielles par type. La distribution compte plus que le total : quelques
   opérations dominent toujours un microcode audio (multiplication-accumulation,
   saturations, permutations).
3. Écrire un banc d'essai isolé qui exécute `dkrAspMain` sur une tâche audio
   capturée depuis une vraie session de jeu, en boucle, et mesure le temps par
   tâche.
4. Mesurer ce banc dans trois configurations :
   - SSE2, 64 bits — la référence actuelle ;
   - scalaire portable, 32 bits sans SSE — le pire cas ;
   - MMX, 32 bits — la cible probable, au moins pour les opérations dominantes
     relevées à l'étape 2.
5. Rapporter le résultat au budget réel : DKR produit de l'audio à cadence fixe ;
   convertir le temps par tâche en pourcentage d'une image de 33,3 ms sur un
   Pentium II à 400 MHz, en réutilisant le facteur de normalisation de E00-S03.
6. Statuer sur le repli : si le microcode dépasse son budget même en MMX,
   E03-S03 (mixeur de haut niveau) passe de contingence à chemin critique.

## Critères d'acceptation

- [ ] Le jeu d'instructions exigé par `rsp_vu_impl.hpp` est établi par lecture du
      code, et l'existence ou non d'un repli scalaire est tranchée.
- [ ] La distribution des opérations vectorielles d'`aspMain` est comptée.
- [ ] Le banc d'essai mesure les trois configurations sur une tâche audio réelle.
- [ ] Le coût est exprimé en pourcentage du budget d'une image sur la cible.
- [ ] Le document conclut : microcode recompilé conservé, ou mixeur de haut
      niveau — avec le chiffre qui motive la conclusion.
- [ ] Le résultat est reporté dans le budget global de E00-S03.

## Risques

Le microcode audio est du code généré et intouchable directement
(`docs/ARCHITECTURE.md`) : toute correction passe par le pipeline de patchs. Si
l'émulation vectorielle doit changer, elle change dans `librecomp` via
`patches/n64-modern-runtime/`, pas dans le fichier généré.

## Références

- `runtime-recomp/RecompiledRSP/aspMain.cpp` — 73 Ko de microcode audio recompilé
- `runtime-recomp/rsp/aspMain.us.v77.toml` — configuration de la recompilation RSP
- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — le microcode graphique, lui, est HLE
- `docs/F3DDKR.md`
