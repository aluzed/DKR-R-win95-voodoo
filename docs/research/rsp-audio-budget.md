# Coût du microcode audio RSP sur la machine cible

Mesures de [E00-S04](../stories/E00-cadrage/E00-S04-spike-cout-rsp-sans-sse.md).
Date : 2026-08-11.

## Résumé

| Grandeur | Mesure |
|---|---|
| Repli scalaire dans `librecomp` | **existe déjà**, sélectionné automatiquement en 32 bits |
| Jeu d'instructions exigé par le chemin SIMD | **SSE4.1** — hors de portée de tout Pentium |
| Compilation du microcode en 32 bits sans SSE | ✅ sans modification, sans instruction SSE |
| Coût d'une opération vectorielle, hôte SIMD | **2,22 ns** (moyenne pondérée) |
| Coût d'une opération vectorielle, cible SISD | **410 ns** (moyenne pondérée) |
| Pénalité du chemin scalaire seul | **10,4×** |
| Débit vectoriel de la cible | **2,44 M op/s** |
| Débit vectoriel du RSP réel | ~62,5 M op/s |
| **Ce que la cible atteint du RSP** | **3,9 %** |

**Conclusion : le microcode audio recompilé ne peut pas tenir le temps réel sur
la cible.** [E03-S03](../stories/E03-rsp/E03-S03-repli-mixeur-haut-niveau.md) —
le mixeur audio de haut niveau — cesse d'être une contingence et devient
nécessaire.

## Le repli scalaire existait déjà

L'étape 1 du ticket demandait si `librecomp/rsp_vu_impl.hpp` offre une
alternative au SIMD. La réponse est dans `rsp_vu.hpp` :

```c
#if defined(__x86_64__) || defined(_M_X64)
#define ARCHITECTURE_SUPPORTS_SSE4_1 1
#include <nmmintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#define ARCHITECTURE_SUPPORTS_SSE4_1 1
#include <sse2neon.h>
#endif

namespace Accuracy { namespace RSP {
#if ARCHITECTURE_SUPPORTS_SSE4_1
    constexpr bool SISD = false;  constexpr bool SIMD = true;
#else
    constexpr bool SISD = true;   constexpr bool SIMD = false;
#endif
}}
```

Sur x86 **32 bits**, aucune des deux conditions d'architecture n'est vraie : la
macro reste indéfinie et `Accuracy::RSP::SISD` vaut `true`. Le chemin scalaire —
une boucle sur les huit voies de 16 bits — est donc retenu **automatiquement**,
sans une ligne à écrire. `aspMain.cpp` compile pour Pentium II sans SSE du
premier coup, et le désassemblage ne contient aucune instruction SSE.

Détail qui clôt une question du ticket : le chemin vectoriel exige **SSE4.1**
(`_mm_shuffle_epi8`, `<nmmintrin.h>`), pas seulement SSE2. Il était hors de
portée de toute la gamme Pentium, pas seulement du Pentium II. La question
« MMX ou scalaire » ne se posait donc jamais comme un choix entre deux
implémentations existantes : il n'y a jamais eu que le scalaire.

## Profil du microcode

`aspMain.cpp` compte **1 061 instructions de microcode**, dont **184
vectorielles** (17 %). Distribution :

| Opération | Occurrences | | Opération | Occurrences |
|---|---:|---|---|---:|
| `vmadh` | 33 | | `vmadm` | 6 |
| `vmulf` | 26 | | `vmudm` | 5 |
| `vxor` | 24 | | `vaddc` | 5 |
| `vmadn` | 17 | | `vmudl` | 4 |
| `vmacf` | 14 | | `vge` | 4 |
| `vadd` | 13 | | `vcl` | 4 |
| `vmudn` | 10 | | `vmudh` | 3 |
| `vand` | 8 | | `vsub` | 2 |
| `vsar` | 6 | | | |

Les huit opérations les plus fréquentes couvrent 80 % du total. C'est le profil
d'un mixeur : multiplication-accumulation sur des échantillons 16 bits.

## Mesure

`tools/cpu-budget/bench_rspvu.cpp` appelle ces huit opérations dans les mêmes
proportions, sur des registres remplis de valeurs de l'ordre des échantillons
audio — pas de motifs dégénérés, dont les saturations ne seraient pas
représentatives. Le même source se compile pour l'hôte (SIMD) et pour la cible
(SISD) sans changement : seule l'architecture décide.

| Opération | Hôte SIMD | Cible SISD | Facteur | Poids |
|---|---:|---:|---:|---:|
| `vadd` | 1,37 ns | 420,79 ns | 307× | 13 |
| `vxor` | 0,58 ns | 143,55 ns | 248× | 24 |
| `vmacf` | 3,33 ns | 641,64 ns | 193× | 14 |
| `vmulf` | 2,34 ns | 443,45 ns | 190× | 26 |
| `vmadn` | 3,27 ns | 584,15 ns | 179× | 17 |
| `vmadh` | 3,10 ns | 498,74 ns | 161× | 33 |
| `vand` | 0,91 ns | 145,38 ns | 160× | 8 |
| `vmudn` | 1,75 ns | 248,07 ns | 142× | 10 |
| **Pondéré** | **2,22 ns** | **410,07 ns** | **185×** | |

Le facteur brut de 185× se décompose : **17,7×** viennent de la machine (mesurés
indépendamment par [E00-S03](cpu-budget.md)), le reste — **10,4×** — est la
pénalité propre au chemin scalaire face aux huit voies traitées d'un coup par
SSE4.1. Les deux termes se recoupent proprement, ce qui donne confiance dans la
mesure.

## Ce que cela veut dire

Le RSP tourne à 62,5 MHz et émet jusqu'à une opération vectorielle par cycle,
soit **~62,5 millions par seconde**. La cible en soutient **2,44 millions** :
elle atteint **3,9 %** du débit vectoriel de la puce qu'elle doit remplacer.

Exprimé en budget d'image, à 30 images par seconde :

| Part du RSP consommée par l'audio sur console | Coût sur la cible | Budget de 33,3 ms |
|---|---:|---:|
| 5 % | 43 ms | **128 %** |
| 10 % | 85 ms | **256 %** |
| 20 % | 171 ms | **513 %** |

Même dans l'hypothèse la plus favorable, l'audio à lui seul dépasse le budget
d'une image entière. Il faudrait que DKR n'utilise que **moins de 4 %** du RSP
pour son audio, ce qui n'est pas crédible pour un jeu avec musique, moteurs et
effets simultanés.

Une implémentation MMX, envisagée par le ticket, ne change pas la conclusion :
MMX offre quatre voies de 16 bits contre huit pour SSE, et ne dispose ni des
permutations d'octets ni des saturations 32 bits dont le chemin SIMD se sert. Un
gain de 3 à 4× amènerait la cible à ~15 % du débit du RSP — toujours cinq fois
trop lent. **Écrire du MMX ne sauverait pas ce chemin**, et c'est une économie
d'effort utile à acter maintenant.

## Décision

[E03-S03](../stories/E03-rsp/E03-S03-repli-mixeur-haut-niveau.md) — interpréter
les commandes audio à haut niveau plutôt qu'exécuter le microcode — **passe de
contingence à chemin critique**. Sa condition de déclenchement, écrite dans
[E03-S02](../stories/E03-rsp/E03-S02-microcode-audio-aspmain.md), est remplie
avec une marge qui ne laisse pas de doute.

Conséquences sur le backlog :

- [E03-S01](../stories/E03-rsp/E03-S01-emulation-vectorielle-sans-sse.md)
  (réimplémentation vectorielle) perd sa raison d'être pour l'audio. Le chemin
  scalaire existe et suffit à *exécuter* le microcode — hors temps réel, ce qui
  reste utile comme **oracle** pour valider le mixeur de E03-S03 : il produit la
  sortie exacte, lentement.
- L'effort estimé de E03 se déplace de « optimiser l'émulation vectorielle » vers
  « écrire un mixeur », ce qui est un poste bien plus lourd — le ticket le classe
  XL.

## Limites

- La cible est un Pentium II **émulé**. Le facteur machine de 17,7× vient du
  modèle de 86Box ([E09-S04](../stories/E09-qa/E09-S04-validation-materiel-reel.md)
  dira de combien il se trompe). Mais l'écart mesuré ici — un facteur 26 sur le
  débit — est trop grand pour qu'une imprécision de modèle le renverse.
- Le banc mesure les opérations isolément, hors du microcode réel. Il ne tient
  pas compte des instructions scalaires du RSP, qui s'ajoutent au coût. La
  mesure est donc **optimiste** : le microcode complet coûterait davantage.
- La part du RSP réellement consommée par l'audio de DKR n'est pas mesurée ; le
  tableau ci-dessus la fait varier plutôt que de la supposer.
