# E01-S02 — Sous-ensemble C++ imposé et dépendances à la bibliothèque standard

| | |
|---|---|
| **Épic** | E01 — Chaîne de build 32 bits Windows 95 |
| **Statut** | REVIEW |
| **Priorité** | P0 |
| **Estimation** | ~~L~~ **M** |
| **Dépend de** | E00-S01, E00-S02, E01-S01 |
| **Bloque** | E02-S01, E02-S02, E04-S01 |

## État au 2026-08-12 — livré

[`docs/CPP-SUBSET.md`](../../CPP-SUBSET.md), vérificateur
`tools/win95/check-cpp-subset.py` branché en **pré-build**.

### Le décompte, seule mesure d'avancement honnête

| Dépendance | Fichiers | Erreurs | Reste |
|---|---:|---:|---|
| **`ultramodern`** | 15 | **0** | — |
| **`librecomp`** | 26 | **6** | attribuées ci-dessous |

**`ultramodern` compile pour Windows 95** avec un seul patch de deux hunks,
`patches/n64-modern-runtime/0014-build-for-windows-95-targets.patch` :

- `std::quick_exit` n'existe pas — mingw ne la déclare que sous `_UCRT`, et la
  cible lie le msvcrt hérité. Remplacée par `std::_Exit`, ce que la branche
  `__APPLE__` juste au-dessus choisit déjà ;
- `SetThreadDescription` est de Windows 10 et ne fait que nommer un fil pour un
  débogueur qui n'existe pas ici. L'appel est supprimé, pas émulé.

Vérifié sur les deux cibles : **0 erreur en Windows 95, 0 erreur sur l'hôte
Linux 64 bits.** Un patch qui casse l'amont casse l'oracle.

La prédiction de E00-S01 — « `ultramodern` se patche, il ne se réécrit pas » —
est confirmée : deux hunks.

**`librecomp` : 6 erreurs, toutes attribuées.**

| Erreur | × | Nature | Suite |
|---|---:|---|---|
| `static_assert(sizeof(std::size_t) == 8)`, `mods.hpp:53` | 4 | hypothèse 64 bits réelle | système de mods, candidat au fork |
| `rabbitizer.hpp` introuvable | 2 | dépendance non récupérée | E01-S05 |

### Compiler n'est pas se charger

`ultramodern` compile sans erreur **et reste inchargeable** : il inclut encore
`<thread>`, `<mutex>` et `<filesystem>`, soit **9 inclusions interdites** que le
vérificateur relève. Une inclusion interdite compile parfaitement et ne se
manifeste qu'au chargement sur la machine cible.

C'est exactement le travail de **E02-S01**, et c'est pourquoi le contrôle
s'exécute en pré-build sur les sources de la cible plutôt qu'en post-lien.

### Il n'y a pas de norme à restreindre

C++20 est autorisé **en entier**. `<format>`, `<ranges>`, `<span>`, `<bit>` et
`<atomic>` sont mesurés à **0 symbole absent**. Ce qui est interdit tient à la
table d'imports, pas au dialecte.

**Exceptions et RTTI conservées** : le témoin T3b les exerce sous Windows 95.
Détail qui explique beaucoup : ce sont elles qui font importer `GetThreadId` par
`libstdc++` — on ne peut donc pas échapper à la couche de compatibilité en
évitant `std::thread`.

### Une correction

`docs/research/win95-blockers.md` affirmait qu'aucune hypothèse 64 bits ne
subsistait. C'était faux : ma recherche employait `sizeof(size_t)` sans accepter
le préfixe `std::`. Compiler réellement `librecomp` l'a révélée. Un balayage
corrigé n'en trouve qu'une, et le document est rectifié.

## État antérieur — la question de départ était mal posée

[E00-S01](../E00-scoping/E00-S01-inventory-of-incompatible-dependencies.md) a
mesuré, et le postulat de ce ticket ne tient pas : **il n'y a pas de
sous-ensemble C++ à imposer.** GCC 13 cible i686 PE32 et implémente tout C++20 ;
concepts, `<ranges>`, `<span>`, `consteval` et `operator<=>` ne coûtent rien à
l'exécution et ne bloquent rien.

Ce qui bloque, ce sont des **facilités de bibliothèque**, mesurées par table
d'imports :

| Facilité | API absentes de Win95 |
|---|---:|
| `printf`, `std::atomic` | **0** |
| `std::chrono` | 2 |
| `std::mutex`, `condition_variable` | 6 |
| `std::thread` | 7 |
| `std::filesystem` | **13** |

Le ticket doit donc être reformulé : non pas « quel dialecte s'interdire », mais
**« quelles facilités de bibliothèque remplacer »** — en pratique
`std::filesystem` (400 sites d'appel, dont 139 disparaissent avec RT64 éteint) et
la couche threads (E02-S01).

Détail : [`docs/research/win95-blockers.md`](../../research/win95-blockers.md).

## Contexte

L'ADR de E00-S02 fixe la norme C++ disponible. Deux issues très différentes :

- **C++17 ou C++20 tiennent** — `ultramodern` et `librecomp` se patchent à la
  marge, et l'essentiel du travail se déplace vers la bibliothèque standard :
  quelles parties de la libstdc++ ou de la libc++ fonctionnent réellement sous
  Windows 95, en particulier les fils d'exécution et la synchronisation.
- **C++98 seulement** — `ultramodern` et `librecomp` sont à réécrire, et le
  projet double de taille. Le compte de constructions C++20 par fichier établi en
  E00-S01 dit alors quoi réécrire en premier.

Dans les deux cas, une règle doit être écrite puis **outillée** : sans
vérification automatique, une construction interdite se réintroduit à la première
contribution et ne se découvre qu'au lien, ou pire, à l'exécution.

## Objectif

Fixer le sous-ensemble C++ autorisé pour la cible Win95, le faire respecter
mécaniquement, et rendre `ultramodern` et `librecomp` compilables dans ce
sous-ensemble.

## Périmètre

**Dans :** la règle, son outillage, et les patchs de dépendances nécessaires.

**Hors :** la couche système elle-même (E02-S01) et le code de rendu (E04, E05).

## Travail

1. Écrire `docs/CPP-SUBSET.md` : normes autorisées, en-têtes standard autorisés,
   en-têtes interdits avec le remplacement à utiliser pour chacun. Les candidats
   à l'interdiction, à confirmer par la mesure de E00-S02 plutôt que par
   présomption : `<thread>`, `<mutex>`, `<condition_variable>`, `<filesystem>`,
   `<format>`, `<ranges>`, `<latch>`, `<barrier>`, `<semaphore>`.
2. Trancher l'usage des exceptions et de la RTTI. Le témoin T3 de E00-S02 a déjà
   la réponse pour la toolchain retenue. Les désactiver réduit la taille du
   binaire — ce qui compte sur cette cible — mais impose de vérifier que le code
   conservé n'en dépend pas.
3. Écrire le vérificateur de sous-ensemble : un script qui parcourt les sources de
   la cible Win95 et échoue sur un en-tête interdit. Le brancher en pré-build.
4. Compiler `ultramodern` avec la toolchain de E01-S01 et traiter les erreurs
   dans l'ordre décroissant de fréquence. Toute correction passe par
   `patches/n64-modern-runtime/`, jamais par une édition directe du worktree —
   c'est la règle du dépôt (`docs/ARCHITECTURE.md`).
5. Même travail pour `librecomp`, en isolant à part son émulation vectorielle du
   RSP : elle est traitée par E03-S01 et ne doit pas bloquer ce ticket.
6. Repousser derrière l'interface de E02-S01 tout ce qui relève des fils
   d'exécution et de la synchronisation, plutôt que de le corriger sur place.
   Ce ticket prépare le terrain ; E02-S02 le remplit.
7. Tenir le décompte des erreurs de compilation restantes, dépendance par
   dépendance, dans le ticket. C'est la seule mesure d'avancement honnête ici.

## Critères d'acceptation

- [ ] `docs/CPP-SUBSET.md` existe : normes, en-têtes autorisés, en-têtes interdits
      avec leur remplacement.
- [ ] La décision sur les exceptions et la RTTI est prise et justifiée.
- [ ] Le vérificateur de sous-ensemble s'exécute en pré-build et échoue sur un
      en-tête interdit introduit volontairement.
- [ ] `ultramodern` compile pour la cible Win95, ou ses erreurs restantes sont
      dénombrées et attribuées à un ticket nommé.
- [ ] `librecomp` compile pour la cible Win95, hors émulation vectorielle du RSP
      explicitement renvoyée à E03-S01.
- [ ] Toutes les modifications de dépendances sont des patchs sous `patches/`,
      référencés depuis `patches/manifest.json`.
- [ ] Les cibles modernes compilent toujours avec les mêmes patchs appliqués —
      un patch qui casse l'amont casse l'oracle.

## Risques

La tentation sera de modifier directement les worktrees de dépendances pour
avancer vite. `Prepare-DKR-Runtime` les recrée, et le travail disparaît sans
prévenir. Le pipeline de patchs n'est pas une formalité administrative : c'est le
seul endroit où les modifications survivent.

## Références

- `docs/ARCHITECTURE.md` — frontières protégées et pipeline de patchs
- `patches/manifest.json` — 13 patchs `n64-modern-runtime` déjà en place, dont
  `0001-use-msvc-compatible-warning-options.patch` : le précédent existe
- E00-S01 — compte des constructions C++20 par fichier
