# Ce que `<filesystem>` coûte réellement sous Windows 95

Mesures de [E02-S05](../stories/E02-systeme/E02-S05-sauvegardes-eeprom-controller-pak.md),
prises sur la machine de test.

Ce dépôt bannissait `<filesystem>` en bloc, en lui attribuant treize symboles
absents. **La règle était fausse**, et coûteuse : elle aurait imposé de réécrire
250 usages d'un type qui fonctionne parfaitement.

## Trois sondes, trois réponses différentes

| Ce que fait le programme | Symboles bloquants | Le binaire se charge ? |
|---|---:|---|
| `#include <filesystem>` seul | **0** | oui |
| un objet `std::filesystem::path` | **1** — `LoadLibraryW` | **oui** — c'est un bouchon |
| un appel à `exists()` | **17** | **non** — 7 sont absents |

La distinction entre les deux dernières lignes est celle qui décide de tout, et
c'est celle que le relevé des bouchons de [E02-S01](win95-blockers.md) permet de
faire :

- un **bouchon** est exporté et ne fait rien. Le chargeur est content, le
  programme démarre.
- un symbole **absent** empêche le processus de démarrer, et Windows nomme le
  symbole dans une boîte d'erreur.

Le détail des dix-sept :

```
BOUCHON  CreateFileW  DeleteFileW  GetDiskFreeSpaceExW  GetFileAttributesW
         GetFullPathNameW  GetTempPathW  GetVolumeInformationW  LoadLibraryW
         MoveFileExW  RemoveDirectoryW
ABSENT   CreateHardLinkW  FindFirstVolumeW  FindNextVolumeW  FindVolumeClose
         GetFileSizeEx  MSVCRT:_fstat64  MSVCRT:_wstat64
```

## `std::filesystem::path` fonctionne, vérifié sur la machine

Le type ne tire qu'un bouchon, donc le binaire se charge. Restait à savoir si ce
que `libstdc++` en fait tient. Relevé de `FSPATH.EXE` sur Windows 95 :

```text
path                : D:\JEU\SAUVE.DAT
parent_path         : D:\JEU
filename            : SAUVE.DAT
extension           : .DAT
concatenation       : D:\JEU\SAUVE.DAT\AUTRE.DAT
verdict             : UTILISABLE
```

Construction, décomposition, concaténation : tout est juste. `path` est de la
manipulation de chaînes, et la manipulation de chaînes ne demande rien au
système.

## Ce que cela change au travail

L'inventaire des usages, sur `ultramodern`, `librecomp` et `runtime-recomp/src` :

| | Occurrences | À faire |
|---|---:|---|
| `std::filesystem::path` — le **type** | **250** | **rien** |
| opérations — `remove`, `exists`, `create_directories`, `copy_file`, `rename`, `is_directory`, `directory_iterator`, `file_size`, `absolute`… | ~140 | à router vers `platform/win95/fileio.h` |

Bannir l'en-tête aurait donc fait réécrire les 250 pour un gain nul, **et** laissé
croire le problème résolu tant que subsistaient les 140 qui comptent. C'est
l'inverse de ce qu'il faut : on aurait payé cher pour se tromper.

Le contrôleur de sous-ensemble surveille désormais les **opérations**, et elles
seules. Son auto-test l'éprouve dans les deux sens : un `path` doit passer, un
`exists()` doit être refusé — parce qu'un contrôleur trop strict finit désactivé,
et qu'un contrôleur trop laxiste ne sert à rien.

Effet immédiat : `ultramodern` **n'a plus aucune inclusion interdite**, et son
cliquet `--max 1` a été retiré — le contrôleur l'a signalé lui-même. Restent 23
signalements dans `librecomp` et 112 dans les sources du jeu, qui sont le travail
réel de E02-S05 et de E02-S02.

## Reproduire

```sh
# les trois sondes
printf '#include <filesystem>\nint main(){return 0;}\n' > p1.cpp
printf '#include <filesystem>\nstatic std::filesystem::path p;\nint main(){return (int)p.string().size();}\n' > p2.cpp
printf '#include <filesystem>\nint main(){return (int)std::filesystem::exists("a");}\n' > p3.cpp

i686-w64-mingw32-g++-posix -std=c++20 -O2 -march=pentium2 -mno-sse -static \
  -static-libgcc -static-libstdc++ -D_WIN32_WINNT=0x0400 -o p1.exe p1.cpp \
  -Wl,--whole-archive build/win95/libwin95compat.a -Wl,--no-whole-archive

python3 tools/win95/check_imports.py p1.exe p2.exe p3.exe
```
