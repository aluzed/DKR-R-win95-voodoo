# `std::ofstream(path)` ne fonctionne pas sous Windows 95

Mesure de [E02-S05](../stories/E02-system/E02-S05-eeprom-and-controller-pak-saves.md),
prise sur la machine de test le 13 août 2026.

## Le relevé

`tools/win95/witnesses/wide_stream_probe.cpp`, exécuté sur la machine :

```text
  ofstream(path)                     : ECHEC
  ofstream(path.string())            : OK
  ofstream(litteral etroit)          : OK
  fopen etroit                       : OK
  ifstream(path)                     : ECHEC
  ifstream(path.string())            : OK
```

## Pourquoi

Sous MinGW, `std::filesystem::path::value_type` est `wchar_t`. Passer un `path`
à un constructeur de flux choisit donc la surcharge large, qui ouvre le fichier
par `_wfopen` — et Windows 9x exporte toute cette famille sous forme de
**bouchons** : le symbole est là, le binaire se charge, et l'ouverture échoue.

C'est la troisième catégorie d'API indisponible, et la plus coûteuse à
diagnostiquer :

| | Le binaire se charge ? | Le contrôle des imports le voit ? |
|---|---|---|
| symbole **absent** | non | oui, il le nomme |
| symbole **bouchonné** | oui | oui, s'il est déclaré |
| symbole **présent qui refuse** | oui | **non** |

`_wfopen` relève de la deuxième. Le contrôle des imports ne s'en plaint pas
puisque le symbole existe ; rien ne distingue l'appel qui réussit de celui qui
échoue, sinon l'exécution.

## Comment il s'est manifesté

Pas par un message clair. La suite `save_manager_tests`, qui passe sur l'hôte,
mourait sur la machine :

```text
reset failed: Could not create the temporary save file.
Assertion failed: false, file runtime-recomp/tests/save_manager_tests.cpp, line 68
```

Un `std::ofstream` qui ne s'ouvre pas, sans code d'erreur exploitable. Le
raisonnement menait au bon endroit, mais **cette plate-forme a déjà démenti cinq
suppositions de ce dépôt** : la sonde a donc été écrite avant la correction.

## Ce que cela a changé

`path.string()` est étroit partout et rend les mêmes octets ailleurs : la
correction ne coûte rien aux cibles qui fonctionnaient déjà.

| | Sites |
|---|---:|
| sources du jeu et suites d'épreuve | 20 |
| cœur de `librecomp` (correctif 0019) | 5 |
| système de mods de `librecomp` — hors périmètre | 3 |

`check-cpp-subset.py` refuse désormais un flux construit sur autre chose qu'une
chaîne étroite. Son auto-test l'éprouve dans les deux sens, et deux dérogations
écrites couvrent les cas qu'un contrôle textuel ne peut pas trancher — une
variable nommée `temporary` qui est déjà une `std::string`.

## Et un second bouchon, sur le même chemin

Avec les flux ouverts correctement, la suite est allée plus loin puis a échoué
autrement :

```text
reset failed: Could not activate the imported save:
              Cette fonction n'est valide qu'en mode Win32
```

C'est l'erreur 120, `ERROR_CALL_NOT_IMPLEMENTED`, en français : **`MoveFileExW`**.
`save_manager::ReplaceFileAtomic` l'appelait directement sous `#if defined(_WIN32)`.

E02-S05 avait déjà mesuré que `MoveFileExA` refuse sous Windows 95 — c'est la
raison d'être de la séquence d'écriture durable — mais l'appel direct échappait
au point d'indirection. Il prend maintenant le repli, et le nom de la fonction
est démenti dans un commentaire : **sur cette cible, le remplacement n'est pas
atomique**, et c'est pourquoi l'appelant prend une copie de secours d'abord.

## Et `<fstream>` lui-même ne se chargeait pas

Trouvé en chemin, et plus grave encore : la simple inclusion de `<fstream>`
rendait le binaire inchargeable. `basic_file.o` de libstdc++ importe
`__imp___fstat64`, que la MSVCRT de Windows 95 n'exporte pas — elle n'a que la
famille `_fstat` d'origine.

Treize fichiers du projet incluent `<fstream>`, dont `recomp.cpp`, `pi.cpp` et
`sp.cpp` de `librecomp` : sans correction, le jeu ne se serait pas lié pour
cette cible.

`platform/win95/compat.c` fournit donc `_fstat64`, bâtie sur `GetFileType` et
`GetFileSize`. Ce dont libstdc++ se sert est étroit — `st_mode` pour savoir si
le descripteur désigne un fichier ordinaire, `st_size` pour dire combien
d'octets restent à lire — et le reste de la structure est mis à zéro plutôt que
rempli au jugé : une date fausse aurait l'air d'une donnée.

## Reproduire

```sh
i686-w64-mingw32-g++-posix -std=c++20 -O2 -march=pentium2 -mno-sse -static \
  -static-libgcc -static-libstdc++ -D_WIN32_WINNT=0x0400 \
  -o WPROBE.EXE tools/win95/witnesses/wide_stream_probe.cpp \
  -Wl,--whole-archive build/win95/libwin95compat.a -Wl,--no-whole-archive

scripts/Push-To-Win95-VM.sh WPROBE.EXE     # puis l'exécuter, lire D:\WPROBE.TXT
```
