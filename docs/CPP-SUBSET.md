# Sous-ensemble C++ de la cible Windows 95

Livré par [E01-S02](stories/E01-build/E01-S02-enforced-cpp-subset.md).
Vérifié par `tools/win95/check-cpp-subset.py`, en pré-build.

## La question de départ était mal posée

Le ticket demandait « quel dialecte C++ s'interdire », en supposant que la cible
imposerait C++98. **C'est faux.** GCC 13 cible `i686-w64-mingw32` et implémente
tout C++20 ; concepts, `<ranges>`, `<span>`, `consteval` et `operator<=>` ne
coûtent rien à l'exécution et ne bloquent rien
([ADR 0001](adr/0001-toolchain.md)).

**Il n'y a donc aucune norme à restreindre. C++20 est autorisé en entier.**

Ce qui bloque n'est pas le langage mais des **facilités de bibliothèque**, parce
qu'elles importent des fonctions que Windows 95 n'exporte pas — et sous
Windows 95, un import manquant empêche le processus de démarrer, même si la
fonction n'est jamais appelée.

## Ce qui est interdit, et pourquoi

Chaque entrée est mesurée, non présumée : le nombre est celui des symboles
absents de Windows 95 qu'une simple inclusion suffit à faire apparaître dans la
table d'imports ([E00-S01](research/win95-blockers.md), reproductible par
`tools/win95/probes/build-probes.sh`).

| En-tête | Symboles absents | Remplacement |
|---|---:|---|
| `<thread>` | 6 | `CreateThread` via la couche de E02-S01 |
| `<mutex>` | 6 | `CRITICAL_SECTION` via la couche de E02-S01 |
| `<condition_variable>` | 6 | événements Win32 via la couche de E02-S01 |
| `<shared_mutex>` | 6 | idem |
| `<future>` | 6 | idem |
| `<latch>`, `<barrier>`, `<semaphore>` | 6 | idem |
| `<filesystem>` | **13** | couche fichiers en `...A` (E02-S05) |

Les six symboles du groupe threads sont toujours les mêmes :
`AddVectoredExceptionHandler`, `RemoveVectoredExceptionHandler`,
`GetTickCount64`, `IsDebuggerPresent`, `SetProcessAffinityMask` et
`TryEnterCriticalSection`. Ils sont **fournis** par `platform/win95/` — mais
`std::thread` ne fonctionne pas pour autant : E00-S02 a mesuré qu'un binaire qui
l'utilise se charge et échoue ensuite dans la partie threads. Fournir les
symboles règle le chargement, pas l'exécution.

`<filesystem>` ajoute sept manques de plus, dont l'énumération de volumes et les
liens durs, tous en `...W`.

### Ce qui n'est pas interdit, contrairement à l'attente du ticket

| En-tête | Statut | Preuve |
|---|---|---|
| `<format>` | **autorisé** | 0 symbole absent |
| `<ranges>`, `<span>`, `<bit>`, `<concepts>` | **autorisés** | 0 symbole absent, purement compilation |
| `<atomic>` | **autorisé** | 0 symbole absent — GCC émet `lock cmpxchg` en ligne |
| `<chrono>` | **autorisé** | 2 symboles, fournis par `platform/win95/` |

`<atomic>` mérite d'être souligné : Windows 95 n'exporte pas
`InterlockedCompareExchange`, et l'on pourrait en conclure que `std::atomic` est
mort. Il ne l'est pas — le compilateur n'appelle pas l'API, il émet l'instruction
du 486 directement. Vérifié à l'exécution sur le Pentium II émulé, `compare_exchange`
et 64 bits compris.

## Exceptions et RTTI : conservées

**Décision : les exceptions et le RTTI restent actifs.**

Le témoin T3b de E00-S02 les exerce tous les deux sous Windows 95 — une hiérarchie
virtuelle, un `typeid`, un `throw`/`catch` — et affiche `RTTI : ok`,
`exception : exception rattrapee`.

Les désactiver réduirait la taille du binaire, ce qui compte peu au regard des
14 Mio de marge de [l'ADR 0003](adr/0003-memory-budget.md), et coûterait cher :
`librecomp` et `ultramodern` en dépendent, et les retirer imposerait de réécrire
leur gestion d'erreurs — exactement le travail que ce projet cherche à éviter.

Détail à connaître : c'est **le RTTI et les exceptions** qui font importer
`GetThreadId` à `libstdc++`. C'est ce qui explique que le témoin T3b, qui
n'utilise pourtant que `CreateThread`, ait d'abord échoué au chargement. On ne
peut donc pas échapper à la couche de compatibilité en évitant `std::thread`.

## État de compilation des dépendances

Mesuré avec la toolchain de [E01-S01](stories/E01-build/E01-S01-cmake-i686-toolchain-without-sse.md).
C'est la seule mesure d'avancement honnête ici.

| Dépendance | Fichiers | Erreurs | Reste |
|---|---:|---:|---|
| **`ultramodern`** | 15 | **0** | — |
| **`librecomp`** | 26 | **6** | voir ci-dessous |

### `ultramodern` compile — un patch, deux hunks

`patches/n64-modern-runtime/0014-build-for-windows-95-targets.patch` :

- **`std::quick_exit`** n'existe pas. mingw-w64 ne la déclare que sous `_UCRT` ;
  la cible lie le msvcrt hérité, où elle n'existe sous aucun nom. Remplacée par
  `std::_Exit`, ce que la branche `__APPLE__` juste au-dessus choisit déjà. La
  différence — les gestionnaires `at_quick_exit` ne sont pas exécutés — est nulle
  ici, aucun n'étant enregistré.
- **`SetThreadDescription`** est de Windows 10. Elle ne fait que nommer un fil
  pour un débogueur, et il n'y en a pas. L'appel est supprimé, pas émulé.

Le patch a été vérifié sur les deux cibles : **0 erreur en Windows 95, 0 erreur
sur l'hôte Linux 64 bits**. Un patch qui casse l'amont casse l'oracle.

La prédiction de E00-S01 — « `ultramodern` se patche, il ne se réécrit pas » —
est donc confirmée par la mesure : deux hunks.

### `librecomp` : 6 erreurs, toutes attribuées

| Erreur | Fichiers | Nature | Attribuée à |
|---|---:|---|---|
| `static_assert(sizeof(std::size_t) == 8)` dans `mods.hpp:53` | 4 | **hypothèse 64 bits réelle** | E01-S02, à patcher |
| `rabbitizer.hpp` introuvable | 2 | dépendance non récupérée | E01-S05 |

**L'assertion de `mods.hpp`** empaquette trois valeurs dans un `size_t` de
64 bits pour hacher une définition de hook de mod. En 32 bits, l'empaquetage est
impossible tel quel — il faudrait une fonction de mélange plutôt qu'un décalage.
Elle est dans le **système de mods**, que [E00-S01](research/win95-blockers.md)
désigne déjà comme le premier candidat au fork, et dont la cible Windows 95 n'a
pas besoin.

> **Correction.** `docs/research/win95-blockers.md` affirmait qu'aucune hypothèse
> 64 bits ne subsistait dans le code. C'était faux : la recherche employait
> `sizeof(size_t)` sans accepter le préfixe `std::`. Un balayage corrigé sur
> `ultramodern`, `librecomp`, `N64Recomp/include` et `runtime-recomp/src` n'en
> trouve **qu'une seule**, celle-ci. Le document est rectifié.

**`rabbitizer.hpp`** est le décodeur d'instructions MIPS de N64Recomp, employé
par `mods.cpp` et `recomp.cpp` pour la réécriture d'instructions à chaud. C'est
une dépendance à récupérer, pas une incompatibilité.

### Deux cales qui ne sont pas des patchs

Ces deux-là ne touchent pas le code des dépendances, parce qu'elles ne corrigent
pas le code des dépendances :

- **`platform/win95/include-shim/Windows.h`.** `ultramodern` écrit
  `#include <Windows.h>` avec une majuscule, ce qui est l'usage de Microsoft et
  fonctionne sur un système de fichiers insensible à la casse. mingw-w64 ne
  fournit que `windows.h`, et Linux ne les confond pas. C'est une propriété de la
  machine sur laquelle on compile, pas de Windows 95.
- **`miniz_export.h`** est généré par le CMake de miniz. Son absence est une
  question de configuration de build, à régler en câblant miniz dans la cible.

## Vérifier

```sh
tools/win95/check-cpp-subset.py extern/n64-modern-runtime/ultramodern
tools/win95/check-cpp-subset.py --self-test
```

Le vérificateur s'exécute en pré-build de la cible Windows 95 et échoue sur un
en-tête interdit. Il est éprouvé par injection, comme les deux autres garde-fous :
un vérificateur cassé et un vérificateur satisfait se taisent de la même manière.
