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

## Deux pièges rencontrés en câblant le point d'indirection

Aucun des deux ne concerne `<filesystem>` lui-même, et tous deux valent d'être
écrits parce qu'ils ont failli faire conclure à tort.

### Le vérificateur de jeu d'instructions accusait à tort

Un binaire employant `std::filesystem::path` était refusé avec deux instructions
« hors Pentium II » : `movaps %xmm0,(%eax)` et `movnti %eax,(%edx)`.

Elles n'étaient pas du code. Le lieur place les tables d'exceptions —
`.gcc_except_table` — **à l'intérieur de `.text`**, et `objdump -d` les
désassemble comme le reste ; des octets de données s'y décodent en instructions
que le processeur n'exécute jamais.

Le faux positif n'est pas bénin : il fait échouer un build correct, et la
réaction naturelle devant un garde-fou qui crie à tort est de le désactiver.
`check-instruction-set.sh` suit désormais le symbole courant et ignore les
régions de données. Son auto-test, qui injecte du vrai SSE, refuse toujours.

Ce piège avait failli passer inaperçu dans l'autre sens aussi : la première
sonde `path` n'avait été soumise qu'au contrôle des **imports**, pas à celui des
instructions. Elle s'exécutait sur la machine émulée, ce qui ne prouvait rien
d'un vrai Pentium II.

### `std::random_device` ne fonctionne pas sous Windows 95

La présence de `std::filesystem::path` dans un binaire y fait entrer
`std::random_device::_M_getentropy` de libstdc++, qui appelle `rand_s` de
libmsvcrt, qui appelle **`LoadLibraryW`** puis `GetProcAddress` pour atteindre le
générateur du système.

`LoadLibraryW` est un bouchon. `rand_s` obtient donc un pointeur de fonction nul,
et **appeler `std::random_device` sauterait dedans**.

L'import lui-même est inoffensif — un bouchon n'empêche pas le chargement — et
la manipulation de `path` n'y touche pas, ce que la sonde confirme sur la
machine. Mais la conclusion est à retenir pour la suite : sur cette cible, le
hasard doit venir d'ailleurs. Aucun code du projet n'emploie `random_device`
aujourd'hui.

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

## Ce que l'extension aux sources du jeu a appris — 13 août 2026

Les quatre opérations de `librecomp` ne suffisaient pas aux 59 sites du jeu. En
ajoutant `is_regular_file`, `file_size`, `rename`, `absolute` et l'énumération,
trois choses sont apparues, dont deux qu'aucune relecture n'aurait données.

### `GetFileAttributesExA` est **absente**, pas bouchonnée

C'était le choix naturel pour `file_size` : elle rend attributs et taille sans
ouvrir le fichier. Windows 95 ne l'exporte pas — comme `GetFileSizeEx`. Ce ne
sont pas des bouchons : le chargeur refuse de démarrer le processus entier.

Le contrôle des imports l'a arrêtée avant la machine, ce qui est exactement son
rôle. La leçon est générale : **une API `...A` n'est pas garantie par le seul
fait d'être `...A`.** Ce couple-là a été ajouté par Windows 98, et rien dans son
nom ne le dit.

Le remplacement est `FindFirstFileA`, qui rend la taille sans ouvrir le fichier
non plus — donc sans descripteur qui fuirait ni conflit de partage, ce qui était
la raison du choix initial.

### `DeleteFileA` sur un répertoire ne dit pas ce qu'on croit

`dkr_file_remove` essayait `DeleteFileA`, puis se rabattait sur
`RemoveDirectoryA` si le chemin était un répertoire. Sur Windows 95 l'échec de
`DeleteFileA` sur un répertoire passait par la branche « déjà absent » et la
fonction **rendait succès sans rien effacer**.

Le défaut ne s'est pas montré par un plantage mais par un faux échec ailleurs :
le nettoyage préalable de la suite d'épreuve n'opérait pas, et l'exécution
suivante trouvait l'arborescence de la précédente. Les horodatages du disque de
transfert l'ont désigné — ils étaient restés à l'heure de la veille.

Le correctif interroge le type **avant** d'agir. Un appel de plus, et plus aucune
façon de confondre « rien à faire » avec « je n'ai pas su ».

### `create_directories` ne rend pas « il est là »

Elle rend « j'en ai créé au moins un ». Sur un répertoire déjà présent,
`std::filesystem` rend **false** ; la couche Windows 95 rendait **true**, parce
que pour elle un répertoire déjà présent est légitimement un succès.

Les deux contrats sont justes séparément, et c'est ce qui rend l'écart pernicieux :
la suite d'épreuve le validait, puisqu'elle demandait seulement `true`. C'est le
même piège que `remove` sur un fichier absent, décrit en tête de la suite — et il
avait été tendu deux fois sans être vu.

**Un point d'indirection dont les deux branches diffèrent sur une valeur de
retour est pire que pas de point d'indirection du tout** : le code marche sur
l'hôte et se comporte autrement sur la cible. Les épreuves portent désormais sur
l'effet autant que sur la valeur rendue.

Pour la même raison, `file_size`, `rename` et `absolute` sont enveloppées des
deux côtés : leurs formes sans `error_code` **lèvent** dans la bibliothèque
standard, là où la cible ne le peut pas. Aucun site d'appel n'y perd — tous
emploient déjà la forme à `error_code`.

### En passant : la machine de test recevait des chemins faux

`Drive-Win95-VM.sh type` passait par `xdotool type`, sans traduction. L'invité
étant en AZERTY, `D:\FSSEAM.EXE` y arrivait en `DM"FSSEQ?:EXE` — et Windows
répondait « fichier introuvable », ce qu'on impute volontiers au binaire.

`tools/win95/azerty_keys.py` savait déjà corriger cela, mais n'était pas branché.
Il l'est désormais, parce qu'aucun chemin Windows ne s'écrit sans « : » ni « \ ».

### État

27 contrôles, tous verts sur la machine émulée comme sur l'hôte, la même source
compilée pour les deux. Ce qui est établi n'est pas « les opérations
fonctionnent » mais « elles se comportent comme celles qu'elles remplacent ».
