# Ce que Windows 95 permet pour écrire une sauvegarde

Mesures de [E02-S05](../stories/E02-system/E02-S05-eeprom-and-controller-pak-saves.md),
prises sur la machine de test. Sonde : `tools/win95/witnesses/fileio_probe.cpp`,
relevé écrit sur le volume **FAT16** du disque de transfert — c'est-à-dire le
système de fichiers qui nous intéresse.

## Le relevé

```text
1. Remplacement d'un fichier existant
   MoveFileExA(REPLACE_EXISTING) : ECHOUE, erreur 120
   MoveFileA sur une cible existante : refuse (attendu)

2. Noms de fichiers longs sur le volume FAT16
   creation d'un nom long        : reussit
   relecture par le meme nom     : reussit
   nom court equivalent          : D:\SAUVEG~1.DKR
   relecture par le nom court    : reussit

3. Emplacement de l'executable
   GetModuleFileNameA            : D:\FILEIO.EXE
   repertoire courant            : D:\

4. Ecriture refusee
   ecriture sur A: (vide)        : refusee, erreur 21

5. Place disponible sur D:
   512 octets par secteur, 16 secteurs par unite
```

## `MoveFileExA` : une troisième façon d'être absente

Le ticket annonçait que « `MoveFileEx` avec remplacement n'y est pas disponible ».
**Il avait raison** — contrairement à deux de ses voisins de E02-S03, démentis
par la mesure. Mais la forme de l'indisponibilité mérite d'être regardée, parce
qu'elle échappe aux deux garde-fous du dépôt.

`MoveFileExA` est **exportée** par KERNEL32, donc le contrôle d'imports la laisse
passer. Elle n'est **pas** une entrée vide : son code commence par le même
prologue que `MoveFileA`, `sub edx,edx` puis l'installation de la chaîne SEH,
donc le relevé des bouchons ne la voit pas non plus. C'est une vraie fonction,
qui décide de refuser et pose `ERROR_CALL_NOT_IMPLEMENTED`.

| Catégorie | Exemple | Ce qui la révèle |
|---|---|---|
| Absente de la table d'exports | `TryEnterCriticalSection` | le contrôle d'imports |
| Exportée, entrée vide | `CreateSemaphoreW` | le relevé des bouchons |
| Exportée, vrai code, refuse | **`MoveFileExA`** | **rien d'autre que l'exécution** |

La troisième ne se déduit d'aucune analyse statique. C'est pourquoi ce dépôt fait
tourner des sondes sur la machine plutôt que de raisonner sur des tableaux.

## Conséquence : il n'y a pas de remplacement atomique

La séquence doit donc être écrite à la main, et sa fenêtre assumée :

```
1. écrire      SAUVE.TMP, vider les tampons, fermer
2. effacer     SAUVE.BAK
3. renommer    SAUVE.DAT -> SAUVE.BAK
4. renommer    SAUVE.TMP -> SAUVE.DAT
```

Entre 3 et 4, le fichier final n'existe pas. **La garantie offerte n'est donc pas
« on ne perd jamais la dernière écriture », mais « on ne perd jamais une
sauvegarde valide ».** C'est la distinction qui compte : perdre la dernière
course est désagréable, perdre la progression entière ne se pardonne pas.

À la relecture, l'ordre de préférence est `SAUVE.DAT`, puis `SAUVE.BAK`, et
**jamais** `SAUVE.TMP` : rien ne prouve qu'il soit complet, et le format de DKR-R
ne porte pas de somme de contrôle qui permettrait de le vérifier sans le
modifier. Charger une sauvegarde tronquée se découvrirait bien plus tard et bien
plus mal.

`MoveFileA` refuse d'écraser, comme documenté — d'où l'effacement préalable de
l'étape 2.

## Les noms longs fonctionnent, mais le 8.3 reste la référence

VFAT est actif : un nom de 37 caractères se crée, se relit par son nom long
comme par son alias `SAUVEG~1.DKR`. L'alias tronque l'extension de `.dkrsave`
à `.DKR`, ce qui suffirait à faire diverger deux noms proches.

La couche ne corrige rien et ne suppose rien : `dkr_file_name_is_8dot3`
**répond**, et les noms qu'elle fabrique elle-même — `.DAT`, `.TMP`, `.BAK` sur
une base d'au plus huit caractères — tiennent tous. Un Windows 95 de première
génération sans VFAT, ou un volume monté autrement, reste donc utilisable.

## Emplacement

Il n'y a pas de `%APPDATA%` sous Windows 95. La sauvegarde va à côté de
l'exécutable, trouvé par `GetModuleFileNameA` — **jamais** par le répertoire
courant : lancé depuis le menu Démarrer, un programme hérite d'un courant sans
rapport avec l'endroit où il est installé. Que les deux coïncident dans le relevé
ci-dessus est une propriété du protocole de test, pas du système.

## Erreurs

L'écriture sur un lecteur vide est refusée avec l'erreur 21, `ERROR_NOT_READY` —
exploitable, et distincte de `ERROR_ACCESS_DENIED` et de `ERROR_DISK_FULL`. La
couche les distingue, parce que « disque plein » et « support protégé »
n'appellent pas le même geste chez le joueur.

## Reproduire

```sh
i686-w64-mingw32-g++-posix -std=c++20 -O2 -march=pentium2 -mno-sse -static \
  -static-libgcc -static-libstdc++ -D_WIN32_WINNT=0x0400 \
  -o FILEIO.EXE tools/win95/witnesses/fileio_probe.cpp \
  -Wl,--whole-archive build/win95/libwin95compat.a -Wl,--no-whole-archive
scripts/Push-To-Win95-VM.sh FILEIO.EXE
# dans l'invité : d:\fileio.exe — le relevé atterrit dans D:\FILEIO.TXT
```

## Le disque plein, provoqué pour de bon — 14 août 2026

Le relevé initial vérifiait que les codes d'erreur sont **distincts** et portent
un texte. C'est nécessaire et ce n'est pas suffisant : rien ne prouvait qu'un
support réellement plein rende `DKR_FILE_ERR_NO_SPACE` plutôt que le
`DKR_FILE_ERR_IO` fourre-tout.

```text
cible              : A:\FULL.DAT
taille demandee    : 2097152 octets
code rendu         : 4              (DKR_FILE_ERR_NO_SPACE)
texte              : disque plein
```

### Reproduire

Le disque de transfert a un demi-gigaoctet de libre, ce qui rend l'exercice
impraticable par ce chemin. Une disquette de 1,44 Mo se remplit en une seconde :

```sh
P=~/.local/dkr-win95; VM=$P/vm/dkr-p2-voodoo2
dd if=/dev/zero of=$VM/plein.img bs=1024 count=1440
$P/bin/mformat -i $VM/plein.img -f 1440 ::
head -c 1350000 /dev/urandom > bourrage.dat
$P/bin/mcopy -i $VM/plein.img bourrage.dat ::/BOURRAGE.DAT   # 107 Ko restants

# puis, dans 86box.cfg, sous [Floppy and CD-ROM drives] :
#   fdd_01_fn = plein.img
```

La sonde vise **2 Mo**, c'est-à-dire plus que le volume entier et non seulement
plus que la place restante : une écriture qui tiendrait tout juste ne prouverait
rien de reproductible.

La configuration de la machine est à remettre en l'état après coup — le montage
d'une disquette pleine changerait la référence de toutes les autres épreuves.

## La coupure de courant, provoquée pour de bon — 14 août 2026

Jusqu'ici la promesse de la séquence durable était raisonnée et éprouvée par
simulation. Sur une machine émulée la vraie coupure est à portée : `kill -9` sur
l'émulateur emporte le cache disque de l'invité comme le ferait une prise
arrachée.

Protocole : écriture en boucle de sauvegardes numérotées et sommées, coupure
après 23 secondes — plus de 350 tours — puis redémarrage et relecture.

### Ce que la coupure a laissé

Windows a détecté l'arrêt incorrect et lancé ScanDisk, qui a trouvé **un seul
fichier endommagé** :

```text
Le fichier D:\PWRCUT.TMP est endommagé. Bien que le début du fichier soit
probablement correct, le fichier est endommagé plus loin.
```

C'est exactement le fichier que la séquence sacrifie. `PWRCUT.DAT` et
`PWRCUT.BAK` étaient intacts.

### Le verdict après redémarrage

```text
code de lecture    : 0 (succes)
octets relus       : 512
numero de tour     : 370
verdict            : sauvegarde valide, fichier principal
```

La sauvegarde du tour 370 a survécu, somme de contrôle comprise, et il n'a même
pas fallu recourir à la copie de secours.

### Ce que ce relevé prouve, et ce qu'il ne prouve pas

Il prouve que la séquence tient sous une coupure réelle, que le dégât se porte
sur le fichier temporaire, et que Windows répare le volume au démarrage suivant.

**Il ne prouve pas que la fenêtre ne soit jamais atteinte.** C'est un tirage :
la coupure est tombée pendant l'écriture du `.TMP`, qui occupe l'essentiel du
temps de chaque tour. La fenêtre — entre le renommage de `.DAT` vers `.BAK` et
celui de `.TMP` vers `.DAT` — reste étroite par construction, et c'est tout ce
que la plate-forme permet : `MoveFileExA(REPLACE_EXISTING)` n'y est pas
implémentée. Un tirage qui tomberait dedans laisserait la précédente dans
`.BAK`, et c'est précisément la garantie annoncée : **on ne perd jamais une
sauvegarde valide**, pas « on ne perd jamais la dernière écriture ».

### Une remarque sur la sévérité du protocole

`kill -9` emporte aussi ce que l'émulateur gardait dans le cache de l'hôte, que
le matériel réel aurait déjà écrit. Le protocole est donc **au moins aussi dur**
qu'une coupure véritable, jamais plus doux — le bon sens de l'erreur. Il l'a
montré : la FAT du volume était illisible depuis l'hôte avant que ScanDisk ne
la répare.
