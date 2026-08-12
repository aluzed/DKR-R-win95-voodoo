# Ce qui empêche DKR-R de tourner sous Windows 95

Inventaire de [E00-S01](../stories/E00-cadrage/E00-S01-inventaire-dependances-incompatibles.md).
Date : 2026-08-12.

## Résumé

| Composant | Verdict | Pourquoi | Ticket |
|---|---|---|---|
| Sortie N64Recomp (`RecompiledFuncs`) | **conserver** | C portable sur entiers ; compile et s'exécute déjà en 32 bits sans SSE | — |
| Microcode audio (`aspMain.cpp`) | **conserver** *(compile)* | repli scalaire automatique — mais 25× trop lent, voir [E00-S04](rsp-audio-budget.md) | E03-S03 |
| `std::atomic` | **conserver** | zéro appel système : le compilateur émet `lock cmpxchg` / `cmpxchg8b` en ligne | — |
| Appels Win32 directs du runtime | **conserver** | les 20 API appelées existent toutes sous Win95 OSR2 | — |
| `std::mutex` / `condition_variable` / `thread` | **remplacer** | tirent 6 API absentes, dont les variables de condition de Vista | E02-S01 |
| `std::filesystem` | **remplacer** | tire 13 API absentes ; 400 sites d'appel | E01-S03 |
| Constantes RDRAM de `librecomp` | **patcher** | `allocation_size` vaut **0** en 32 bits ; `mem_size` dépasse le plafond mesuré | E00-S06 |
| API Unicode `...W` | **remplacer** par `...A` | exportées mais **stubs** : retour 0 et `ERROR_CALL_NOT_IMPLEMENTED` | E01-S03 |
| RT64 | **conserver, éteint** | déjà derrière `DKR_RUNTIME_BUILD_RT64`, à `OFF` par défaut | E04-S01 |
| Dear ImGui | **retirer** | 97 % dans un seul fichier, déjà exclu quand RT64 est éteint | E07-S02 |
| SDL2 | **remplacer** | 94 % dans trois fichiers ; pas de portage Win95 viable | E07-S03 |

**Le pronostic est meilleur que ce que le ticket redoutait.** Le cœur du projet — le
code recompilé — passe sans modification. Ce qui tombe est la couche
d'*hébergement* : threads, système de fichiers, fenêtre. Et l'essentiel des
bloquants tient dans **une poignée de fonctions**, pas dans une réécriture.

## Méthode

Le ticket exigeait de vérifier les API manquantes « contre une source d'exports
réelle, pas de mémoire ». C'est ce qui a été fait, et un cran plus loin :

1. Les six DLL système ont été **extraites de la machine de test elle-même**
   (`C:\WINDOWS\SYSTEM`) et leurs tables d'exports analysées :

   | DLL | Taille | Date | Exports |
   |---|---:|---|---:|
   | `KERNEL32.DLL` | 422 400 | 1996-08-24 | 682 |
   | `MSVCRT.DLL` | 280 576 | 1997-11-03 | 756 |
   | `USER32.DLL` | 44 544 | 1996-08-24 | 580 |
   | `GDI32.DLL` | 131 072 | 1996-08-24 | 330 |
   | `ADVAPI32.DLL` | 43 008 | 1996-08-24 | 224 |
   | `WINMM.DLL` | 49 152 | 1996-08-24 | 182 |

   Soit **2 754 symboles** de référence, propres à *cette* installation.

2. Plutôt que de deviner ce que le code appelle, des **sondes ont été compilées**
   pour i686 avec GCC 13 et leur **table d'imports PE** comparée à cette
   référence. C'est la bonne granularité : Windows 95 résout *tous* les imports
   au chargement, donc un symbole absent est fatal même si la fonction n'est
   jamais appelée.

3. Les sondes ont ensuite été **exécutées sur la machine réelle**, pour vérifier
   que la prédiction se réalise.

### Reproduire

```sh
tools/win95/check-win95-imports.sh --refresh   # extrait la reference des DLL de la VM
tools/win95/probes/build-probes.sh             # matrice, modele de threads win32
tools/win95/probes/build-probes.sh posix       # matrice, modele winpthreads
tools/win95/check-win95-imports.sh build/DKR-R.EXE   # controle d'un binaire
```

Le contrôleur renvoie un code de retour non nul s'il manque un symbole : il est
utilisable tel quel comme garde-fou de build
([E01-S04](../stories/E01-build/E01-S04-garde-fou-imports-pe.md)).

## 1. Appels Win32 directs du runtime — **aucun bloquant**

En croisant les identifiants appelés par `ultramodern`, `librecomp` et
`runtime-recomp/src/game/` avec les 2 783 API décorées d'un import de DLL dans
les en-têtes mingw, on obtient **20 API système réellement appelées** :

```
CloseHandle  CreateProcessW  FillRect  FreeLibrary  GetCommandLineW
GetCurrentProcess  GetCurrentThread  GetCurrentThreadId  GetLastError
GetLogicalDrives  GetModuleHandleW  GetProcAddress  LoadLibraryExW
MoveFileExW  SetThreadPriority  SetUnhandledExceptionFilter  Sleep
VirtualAlloc  VirtualFree  VirtualProtect
```

**Les vingt sont exportées par Win95 OSR2.** Le code écrit par le projet ne pose
aucun problème d'API. C'est un résultat important : il localise le travail
ailleurs.

### Mais les cinq variantes `...W` sont des leurres

`CreateProcessW`, `GetCommandLineW`, `GetModuleHandleW`, `LoadLibraryExW` et
`MoveFileExW` figurent bien dans la table d'exports. Elles ne font rien.

Le désassemblage le montre sans ambiguïté. Toutes les entrées `...W` sont tassées
dans ~150 octets autour de la RVA `0x034fb9`, et chacune tient en trois
instructions :

```asm
34fef:  33 c0           xor    eax,eax      ; valeur de retour = 0 (échec)
34ff1:  b1 03           mov    cl,0x3       ; index du stub
34ff3:  e9 21 c3 fc ff  jmp    0x1319       ; queue commune
```

Et la queue commune :

```asm
1319:   51              push   ecx
131a:   68 78 00 00 00  push   0x78         ; 120 = ERROR_CALL_NOT_IMPLEMENTED
131f:   e8 be c7 00 00  call   0xdae2       ; SetLastError
```

Détail révélateur : `LoadLibraryExW` et `MoveFileExW` **partagent la même
adresse** (`0x034fef`). Deux fonctions au comportement radicalement différent
pointent sur le même code — parce qu'aucune des deux n'en a.

**Verdict :** basculer sur les variantes `...A`. C'est mécanique, mais il faut le
faire consciemment : le lien réussit, le chargement réussit, et l'échec se
produit à l'exécution, silencieusement.

## 2. Bibliothèque standard C++ — le vrai gisement

Le ticket supposait que les constructions C++20 « excluent les compilateurs
capables de cibler Win95 ». **C'est faux, et c'est une bonne nouvelle.** GCC 13
cible i686 PE32 et implémente tout C++20 ; concepts, `<ranges>`, `<span>`,
`consteval` et `operator<=>` ne coûtent rien à l'exécution.

Le problème n'est pas le langage, c'est la **bibliothèque** : quelles facilités
tirent des API que Win95 n'a pas. Sept sondes, compilées à l'identique
(`-march=pentium2 -mno-sse -static`), donnent la réponse :

| Sonde | Imports | Bloquants | Détail |
|---|---:|---:|---|
| `printf` seul | 53 | **0** | référence |
| `std::atomic` | 53 | **0** | y compris `compare_exchange` et 64 bits |
| `std::chrono` | 68 | 2 | `GetThreadId`, `TryEnterCriticalSection` |
| `std::mutex` | 86 | 6 | + les 4 variables de condition |
| `std::condition_variable` | 87 | 6 | idem |
| `std::thread` | 107 | 7 | + `_fstat64` |
| `std::filesystem` | 131 | **13** | + volumes, liens durs, `stat64` |

Les six bloquants du groupe threads :

```
InitializeConditionVariable   Vista (2006)
SleepConditionVariableCS      Vista
WakeConditionVariable         Vista
WakeAllConditionVariable      Vista
GetThreadId                   XP
TryEnterCriticalSection       98 / NT 4
```

Les sept que `std::filesystem` ajoute :

```
FindFirstVolumeW  FindNextVolumeW  FindVolumeClose  CreateHardLinkW
GetFileSizeEx     _wstat64         _fstat64
```

### `std::atomic` passe, et c'est vérifié sur la machine

Contre-intuitif : Win95 n'exporte que trois fonctions `Interlocked`
(`Increment`, `Decrement`, `Exchange`) — ni `CompareExchange`, ni `ExchangeAdd`.
On pourrait en conclure que `std::atomic` est mort.

Il ne l'est pas : GCC n'appelle pas ces API, il émet `lock cmpxchg` (486+) et
`cmpxchg8b` (Pentium+) directement. La sonde `std::atomic<int>` +
`compare_exchange_strong` + `std::atomic<long long>` **s'exécute correctement sur
le Pentium II émulé** et affiche `2 1`, la valeur attendue.

C'est le genre de conclusion qu'on ne tire pas d'un tableau de compatibilité.

### Le modèle de threads déplace le problème sans le résoudre

Deux modèles existent pour mingw. Les deux ont été installés et mesurés :

| Sonde | modèle `win32` | modèle `posix` (winpthreads) |
|---|---:|---:|
| `printf`, `std::atomic` | 0 | 0 |
| `std::mutex`, `condition_variable` | 6 | 6 |
| `std::thread` | 7 | 6 |
| `std::chrono` | 2 | 6 |
| `std::filesystem` | 13 | 13 |

Même compte, nature différente. Le modèle `posix` élimine les variables de
condition de Vista, mais en ramène d'autres :

```
AddVectoredExceptionHandler  RemoveVectoredExceptionHandler   XP
GetTickCount64                                                Vista
IsDebuggerPresent                                             98 / NT 4
SetProcessAffinityMask                                        NT
TryEnterCriticalSection                                       98 / NT 4
```

**Le modèle `posix` est préférable**, parce que ses manques sont superficiels :
`IsDebuggerPresent` renvoie faux, `SetProcessAffinityMask` ne fait rien,
`GetTickCount64` s'enveloppe autour de `GetTickCount` en gérant le repli à 49
jours, les gestionnaires d'exceptions vectorisés se rabattent sur
`SetUnhandledExceptionFilter` — qui, lui, existe. Alors que reproduire les
variables de condition de Vista sur des événements Win95 est un exercice
délicat, où l'on perd des réveils.

**Verdict : écrire une petite bibliothèque de compatibilité** qui fournit ces six
entrées, et la placer avant `libkernel32.a` dans l'ordre de résolution. Ce n'est
pas une réécriture de `ultramodern` — c'est une page de code. `TryEnterCriticalSection`
est le seul point qui demande de l'attention, et il s'implémente sur
`InterlockedExchange`, qui est présent.

### Où se concentre l'usage

Compté par fichier, pour que E01-S02 tranche sur un chiffre :

**`ultramodern`** — 15 `.cpp`, **6 fichiers** touchent aux primitives bloquantes :

| Fichier | Usage |
|---|---|
| `src/events.cpp` | `std::thread`×6, `std::mutex`×1 |
| `src/threads.cpp` | `std::thread`×3 |
| `src/timer.cpp` | `std::thread`×2 |
| `src/renderer_context.cpp` | `std::mutex`×3 |
| `src/extensions.cpp` | `std::mutex`×1 |
| `include/ultramodern/ultramodern.hpp` | `std::thread`×1, `std::filesystem`×1 |

**C'est petit.** `ultramodern` se patche, il ne se réécrit pas — 12 `std::thread`
et 5 `std::mutex` en tout. La conclusion vaut d'être posée noir sur blanc,
parce que le ticket E02-S01 était dimensionné sur l'hypothèse inverse.

**`librecomp`** — 26 `.cpp`, dominé par `std::filesystem` (101 usages) :
`mods.hpp`×21, `mods.cpp`×20, `mod_manifest.cpp`×15, `files.cpp`×14,
`recomp.cpp`×13, `mod_config_api.cpp`×6, `pi.cpp`×6. Les deux tiers servent le
système de mods, dont le portage Win95 n'a pas besoin.

**`runtime-recomp/src`** — 299 usages de `std::filesystem`, mais **139 (46 %)
disparaissent** en éteignant RT64 (voir §5). Restent 160, concentrés dans
`save_manager.cpp`×71 et `virtual_pak.cpp`×24 — ceux-là sont nécessaires.

## 3. Jeu d'instructions — contenu

Aucun usage de SSE/AVX hors des chemins déjà traités :

| Fichier | Occurrences | Statut |
|---|---:|---|
| `thirdparty/sse2neon/sse2neon.h` | 1 377 | ARM seulement, jamais compilé sur x86 |
| `librecomp/include/librecomp/rsp_vu_impl.hpp` | 277 | sous `ARCHITECTURE_SUPPORTS_SSE4_1`, repli SISD automatique |
| `thirdparty/xxHash/xxhash.h` | 67 | dispatch à l'exécution, scalaire par défaut |
| `N64Recomp/lib/tomlplusplus` | 11 | détection seulement |

[E00-S03](cpu-budget.md) a compilé le code recompilé pour Pentium II et vérifié
au désassemblage qu'il **n'émet aucune instruction SSE**. Rien à faire ici.

## 4. Hypothèses 64 bits — un bloquant sérieux, et un seul

La recherche de `static_assert(sizeof(void*) == 8)`, de conversions
pointeur↔`uint64_t` et d'hypothèses sur `size_t` ne remonte **rien**. Les
constantes `0xFFFFFFFF80000000` qui parsèment `recomp.h` sont des adresses
*invitées*, calculées en `uint64_t` puis utilisées comme index dans `rdram` :
correct en 32 bits.

Le problème est ailleurs — `librecomp/include/librecomp/addresses.hpp` :

```cpp
constexpr size_t mem_size        =  512ULL * 1024ULL * 1024ULL;
constexpr size_t allocation_size = 4096ULL * 1024ULL * 1024ULL;
```

En 32 bits, `size_t` fait 4 octets. GCC le signale, mais comme un simple
avertissement :

```
warning: conversion from 'long long unsigned int' to 'size_t' {aka 'unsigned int'}
changes value from '4294967296' to '0' [-Woverflow]
```

**`allocation_size` vaut 0.** `recomp.cpp:773` appelle donc
`VirtualAlloc(nullptr, 0, ...)`, qui échoue, et le jeu s'arrête sur « Failed to
allocate memory ». Ça compile, ça se lie, et ça meurt au démarrage.

Vérifié sur la machine réelle, qui répond aussi à la question que le code source
ne peut pas trancher :

| Grandeur | Mesure sur la cible |
|---|---:|
| `sizeof(size_t)` | 4 |
| `allocation_size` évalué | **0** |
| `mem_size` évalué | 536 870 912 |
| RAM physique / disponible | 63 Mio / 47 Mio |
| Espace d'adressage virtuel | 2 044 Mio |
| Granularité d'allocation | 65 536 |
| **Réservation maximale** | **1 024 Mio** |
| **Validation R/W maximale** | **256 Mio** |
| Schéma de `librecomp` à 8 Mio | **OK** |

Deux enseignements pour [E00-S06](../stories/E00-cadrage/E00-S06-adr-budget-memoire.md) :

- même corrigée de la troncature, la réservation de 4 Gio est impossible : le
  plafond mesuré est de 1 Gio ;
- **`mem_size` à 512 Mio échouerait aussi** : la validation en lecture-écriture
  plafonne à 256 Mio sur une machine de 64 Mio.

En revanche, le schéma exact de `librecomp` — réserver, valider une fenêtre,
`VirtualProtect` — **fonctionne à 8 Mio**, soit précisément la RDRAM d'une N64
avec Expansion Pak. Le correctif est donc de rendre ces deux constantes
dépendantes de la cible, pas de changer de mécanisme.

## 5. Dépendances externes

### RT64 — **conserver, éteint**

Rien à retirer : le renderer est déjà optionnel.

```cmake
option(DKR_RUNTIME_BUILD_RT64 "Also configure and compile the pinned RT64 renderer" OFF)
```

Et le bloc `if(DKR_RUNTIME_BUILD_RT64)` exclut, avec lui, `f3ddkr_rt64.cpp`,
`rt64_renderer.cpp`, `runtime_crt_overlay.cpp`, `runtime_rice_texture_import.cpp`,
`runtime_texture_packs.cpp`, `runtime_ui.cpp` et le pont ImGui/SDL.

**Conséquence directe : une bonne part de [E07-S02](../stories/E07-perimetre/E07-S02-depose-imgui-texture-packs-telemetrie.md)
est déjà faite par un interrupteur qui existe.** Le ticket doit être réduit.

RT64 exige D3D12, Vulkan ou Metal ; aucun n'existe sous Windows 95, et une
Voodoo ne parle que Glide. Il n'est pas question de le porter — seulement de ne
pas l'allumer. C'est aussi ce qui protège la cible moderne : elle continue de
l'allumer, sans rien savoir du travail Win95.

### Dear ImGui — **retirer**, et c'est presque gratuit

1 089 références, dont **1 054 dans le seul `runtime_ui.cpp`** (97 %), déjà exclu
quand RT64 est éteint.

Reste un résidu qui n'est *pas* sous garde : `runtime_platform.cpp` contient 31
références ImGui, non conditionnées, qui recopient l'état de la manette dans
`ImGuiIO` (lignes 418-458). Ce fichier fait partie du socle. C'est le seul
véritable travail de découplage d'ImGui, et il est petit.

### SDL2 — **remplacer**

343 références dans 10 fichiers, dont 94 % dans trois : `runtime_platform.cpp`
(177), `runtime_input.cpp` (81), `runtime_ui.cpp` (66, déjà exclu). SDL2 n'a
plus de portage Windows 95 depuis longtemps, et le projet n'en utilise que la
fenêtre, le clavier, la manette et l'audio — soit exactement ce que E06 prévoit
de réécrire en Win32 nu. La concentration en trois fichiers rend l'opération
franche.

## 6. Ce qui survit sans modification

C'est la partie que le ticket demandait de nommer explicitement, parce que c'est
la valeur conservée du projet :

- **Toute la sortie de N64Recomp** (`RecompiledFuncs`, 3 823 fonctions) : du C sur
  entiers, sans dépendance système. Compile pour Pentium II et s'exécute — c'est
  ce que [E00-S03](cpu-budget.md) a mesuré.
- **Le microcode audio recompilé** : compile en 32 bits sans SSE, sans une ligne
  à changer, grâce au repli scalaire de `rsp_vu.hpp`. Il est 25 fois trop lent
  pour le temps réel ([E00-S04](rsp-audio-budget.md)), mais il reste exact —
  c'est l'oracle de E03-S03.
- **`std::atomic`**, y compris 64 bits — vérifié sur la machine.
- **Les 20 API Win32 appelées par le projet.**
- **`std::chrono`**, à deux fonctions près, elles-mêmes triviales à fournir.
- **RT64 et ImGui**, qui n'ont pas besoin d'être touchés : il suffit de ne pas
  les compiler.

## Limites

- L'inventaire porte sur les **sources**, pas sur une compilation complète de
  `ultramodern` et `librecomp` pour i686 — c'est E00-S02. D'autres imports
  peuvent apparaître au lien réel ; la méthode par table d'imports les
  révélera au premier essai.
- La référence d'exports est celle de **cette** installation (Win95 OSR2 + un
  `MSVCRT.DLL` de novembre 1997). Une machine réelle du même âge peut différer
  légèrement — [E09-S04](../stories/E09-qa/E09-S04-validation-materiel-reel.md)
  le dira.
- Le comptage par fichier repose sur des expressions régulières, pas sur l'arbre
  syntaxique : il donne un ordre de grandeur fiable, pas un décompte exact.
- Les sondes mesurent ce que la bibliothèque standard **importe**, ce qui est le
  bon critère sous Win95 puisque le chargement échoue sur un symbole manquant.
  Elles ne disent rien de ce qui se passerait si l'on fournissait ces symboles
  et que la sémantique différait — c'est le risque propre à la bibliothèque de
  compatibilité, à couvrir par des tests.
