# ADR 0001 — Chaîne de compilation

- **Statut** : accepté
- **Date** : 2026-08-12
- **Ticket** : [E00-S02](../stories/E00-scoping/E00-S02-spike-pe-win95-toolchain.md)

## Contexte

Le choix du compilateur commande le langage disponible, et le langage disponible
commande la quantité de code à réécrire. C'est la décision la plus structurante
du projet : si aucun compilateur moderne ne produit un binaire viable sous
Windows 95, `ultramodern` et `librecomp` — écrits en C++20 — deviennent une
réécriture complète.

Elle se tranche par l'expérience. Trois témoins de complexité croissante ont été
construits par trois candidats, puis **exécutés sur la machine de test** :

| Témoin | Ce qu'il prouve |
|---|---|
| **T1** | format PE, sous-système, imports de base — sans CRT |
| **T2** | le CRT : tas, `fopen`/`fread`, `printf`, mathématiques flottantes |
| **T3a** | `std::thread`, `std::mutex`, `condition_variable`, RTTI, exceptions |
| **T3b** | le même modèle d'exécution, mais sur les primitives Win32 |

T3a et T3b sont le cœur de l'affaire : T3a est la forme qu'a le code à porter,
T3b la forme qu'il pourrait prendre.

## Résultats

Tous les binaires sont compilés `-march=pentium2 -mfpmath=387 -mno-sse`, et le
désassemblage confirme **aucune instruction SSE** — code de démarrage et
bibliothèque standard compris.

| Candidat | C++ | T1 | T2 | T3a | T3b | Taille T3b | DLL importées |
|---|---|---|---|---|---|---:|---|
| **Open Watcom 2.0** | C++98 partiel | ✅ | ✅ | *sans objet* | ✅ | 51 200 o | KERNEL32, USER32 |
| **mingw GCC 13, posix** | **C++20** | ✅ | ✅ | ❌ | ✅ *(avec pont)* | 501 625 o | KERNEL32, USER32, MSVCRT |
| **mingw GCC 13, win32** | **C++20** | ✅ | ✅ | ❌ | ❌ | 353 108 o | KERNEL32, USER32, MSVCRT |

Open Watcom n'a pas `<thread>` : T3a n'existe pas pour lui, et c'est un résultat,
pas une lacune du protocole.

### Les échecs, et le symbole exact en cause

Windows 95 résout **tous** les imports au chargement. Un symbole absent est donc
fatal même si la fonction n'est jamais appelée — ce que la machine dit elle-même :

> « Le fichier T3BG.EXE est lié à une exportation manquante KERNEL32.DLL:GetThreadId. »

T3b n'appelle jamais `GetThreadId` : il utilise `CreateThread` directement. C'est
`libstdc++`, tirée par les exceptions et le RTTI, qui l'importe. **On ne peut donc
pas échapper au problème en évitant `std::thread`.**

Symboles manquants relevés avant correction :

| Binaire | Manquants |
|---|---|
| mingw-win32 T3b | `GetThreadId`, `TryEnterCriticalSection` |
| mingw-win32 T3a | + `InitializeConditionVariable`, `SleepConditionVariableCS`, `WakeConditionVariable`, `WakeAllConditionVariable`, `_fstat64` |
| mingw-posix T3a et T3b | `AddVectoredExceptionHandler`, `RemoveVectoredExceptionHandler`, `GetTickCount64`, `IsDebuggerPresent`, `SetProcessAffinityMask`, `TryEnterCriticalSection` |

### Le pont de compatibilité, écrit et éprouvé

Les six manques du modèle `posix` sont superficiels — aucun n'est une
fonctionnalité, tous sont des commodités. `tools/win95/win95compat/` les fournit
en 150 lignes, et **T3b passe alors de « ne démarre pas » à 1000/1000 sur la
machine réelle**, RTTI et exceptions compris.

Deux difficultés ont dû être résolues, et méritent d'être consignées :

**Fournir la fonction ne suffit pas.** `winpthreads` est compilée avec
`__declspec(dllimport)` : ses appels visent le pointeur `__imp__X@n`, pas le
symbole `_X@n`. Le pont doit donc définir aussi ces pointeurs, décoration stdcall
comprise, et être lié en `--whole-archive` — sans quoi `libkernel32.a`, placée en
dernier par les specs du compilateur, l'emporte.

**Un bouchon « licite » n'est pas un bouchon inoffensif.** La première version
faisait renvoyer `FALSE` à `TryEnterCriticalSection` — réponse permise par le
contrat, et vérifiée sans danger puisque `try_lock` n'apparaît nulle part dans le
runtime. **Elle a figé la machine entière** : `winpthreads` boucle sur cette
fonction pour prendre ses verrous, et l'attente active affame l'ordonnanceur de
Windows 95 jusqu'à arrêter l'horloge de la barre des tâches. Le pont implémente
donc les **cinq** fonctions de section critique, ce qui lui donne la propriété des
24 octets de `CRITICAL_SECTION`, et s'appuie sur `lock cmpxchg` — une instruction
du 486 — là où Windows 95 n'exporte pas `InterlockedCompareExchange`.

### Ce que le pont ne répare pas

**`std::thread` ne fonctionne pas, même avec le pont.** T3a se charge désormais
sans erreur, démarre, puis se termine sans écrire son résultat : il échoue dans la
partie threads. Le blocage n'est donc pas seulement une question de symboles
absents — l'émulation POSIX de `winpthreads` ne tient pas sur Windows 95.

C'est le résultat le plus utile de ce spike, parce qu'il transforme une hypothèse
en certitude : **la couche de fils d'exécution doit être réécrite sur les
primitives Win32** ([E02-S01](../stories/E02-system/E02-S01-threading-and-synchronisation-layer.md)).
Ce n'était jusqu'ici qu'un plan plausible ; c'est maintenant une contrainte
mesurée.

## Décision

**mingw-w64 GCC 13, cible `i686-w64-mingw32`, modèle de threads `posix`, lié
avec `win95compat`.**

### Pourquoi pas Open Watcom, qui passe pourtant tout

Watcom est le meilleur candidat sur tous les critères sauf un, et cet unique
critère décide :

- ses binaires font **51 Ko contre 501** ;
- il n'importe **que KERNEL32 et USER32**, sans aucun manque, sans pont ;
- son CRT est lié statiquement — pas de question de redistribution ;
- il cible Windows 95 nativement, sans détour.

Mais il n'offre que **C++98 partiel**. `ultramodern` et `librecomp` sont écrits
en C++20, et [E00-S01](../research/win95-blockers.md) a montré que leur portage
tient en **six fichiers** de `ultramodern` et le remplacement de
`std::filesystem`. Avec Watcom, ce ne sont plus six fichiers à patcher mais deux
bibliothèques entières à réécrire — le scénario que le ticket désignait
explicitement comme le risque à écarter, « plusieurs semaines qui ne figurent
dans aucun ticket ».

Le surcoût de mingw est un binaire dix fois plus gros et un pont de 150 lignes.
Le budget mémoire ([ADR 0003](0003-memory-budget.md)) a 14 Mio de marge : la
taille n'est pas un problème.

**Watcom reste le repli documenté.** Si le portage de `ultramodern` dérape au
point de devenir une réécriture, l'argument qui écarte Watcom tombe — et il
faudra alors le reconsidérer plutôt que persister. C'est la seule condition qui
rouvrirait cette décision.

### Pourquoi le modèle `posix` plutôt que `win32`

Les deux modèles manquent d'un nombre comparable de symboles, mais pas de la même
nature. Le modèle `win32` réclame les **variables de condition de Vista**, dont la
reproduction sur des événements Windows 95 est un exercice délicat où l'on perd
des réveils. Le modèle `posix` réclame `IsDebuggerPresent`, `GetTickCount64`,
`SetProcessAffinityMask` et deux gestionnaires d'exceptions vectorisés — toutes
choses qu'on écrit en quelques lignes, ce qui a été fait et vérifié.

### Distribution du CRT : liaison statique

`-static -static-libgcc -static-libstdc++`, sans exception.

Trois raisons, dont deux mesurées :

1. **`libgcc_s_dw2-1.dll` n'existe pas sous Windows 95.** Un binaire lié
   dynamiquement à libgcc ne se charge pas — constaté pendant ce spike, sur un
   témoin compilé par inadvertance sans `-static`.
2. **`MSVCRT.DLL` est présent sur la machine de test** (version du 3 novembre
   1997, 756 exports) mais **pas dans le Windows 95 de première génération**. Il
   arrive avec OSR2 ou Internet Explorer. En dépendre reviendrait à faire dépendre
   le jeu d'une version d'IE.
3. La liaison statique supprime toute question de redistribution.

Le coût est la taille : 501 Ko pour T3b. Sans objet au regard du budget.

## Conséquences

- **`ultramodern` et `librecomp` sont patchables**, pas à réécrire. Le risque
  majeur identifié par le ticket ne s'est pas matérialisé, et c'est le principal
  acquis de ce spike.
- **[E02-S01](../stories/E02-system/E02-S01-threading-and-synchronisation-layer.md)
  devient obligatoire et non optionnel** : `std::thread` ne fonctionne pas sur la
  cible. La couche de fils doit reposer sur `CreateThread`, `CRITICAL_SECTION` et
  les événements — ce que T3b valide.
- **`tools/win95/win95compat/` est le point de départ de cette couche.** Il est
  écrit, lié et éprouvé sur la machine ; E02-S01 l'étend plutôt que de partir de
  rien.
- **[E01-S01](../stories/E01-build/E01-S01-cmake-i686-toolchain-without-sse.md)**
  hérite des drapeaux exacts :
  `-march=pentium2 -mtune=pentium3 -mfpmath=387 -mno-sse -mno-sse2 -static
  -static-libgcc -static-libstdc++`, plus
  `-Wl,--whole-archive -lwin95compat -Wl,--no-whole-archive`.
- **[E01-S04](../stories/E01-build/E01-S04-pe-import-guard-rail.md)** devient
  indispensable, pas confortable : le seul symbole oublié rend le binaire
  inchargeable, sans avertissement au lien.
- Le contrôle « aucune instruction SSE » doit porter sur le **binaire lié**, pas
  sur les objets du projet : le SSE viendrait de la bibliothèque standard.

## Reproduire

```sh
tools/win95/witnesses/build-witnesses.sh          # les trois candidats, matrice complète
tools/win95/check-win95-imports.sh <binaire>      # symboles absents de Windows 95
scripts/Push-To-Win95-VM.sh build/win95-witnesses/*.exe
```

Open Watcom s'installe sans droits : l'installeur Linux publié par le projet est
une archive zip, qui s'extrait dans `~/.local/dkr-win95/opt/watcom`.

## Références

- [`docs/research/win95-blockers.md`](../research/win95-blockers.md) — inventaire des manques, comptes par fichier
- `tools/win95/witnesses/` — les quatre témoins et leur banc
- `tools/win95/win95compat/win95compat.c` — le pont
- [ADR 0003](0003-memory-budget.md) — budget mémoire, qui rend la taille des binaires indifférente
