# Ce que la couche de compatibilité Windows 95 change

Livré par [E01-S03](stories/E01-build/E01-S03-crt-startup-compatibilite-api.md).
Implémentation : `platform/win95/`.

Ce document existe pour une raison précise : **un contournement dont la
différence n'est pas écrite est un bogue en attente.** Chaque fonction ci-dessous
remplace une API absente de Windows 95, et chacune perd quelque chose. Ce qui est
perdu est écrit, y compris quand la perte est nulle.

## Pourquoi une couche est nécessaire

Windows 95 résout **tous** les imports au chargement. Un symbole absent empêche
le processus de démarrer, avec un message qui nomme la DLL et le symbole — et
rien d'autre. Le code du projet n'appelle aucune des fonctions ci-dessous : ce
sont `libstdc++` et `winpthreads` qui les importent. Leur seule présence dans la
table d'imports suffit à tuer le programme.

L'inventaire est celui de [E00-S01](research/win95-blockers.md), et le choix du
modèle de threads `posix` celui de [l'ADR 0001](adr/0001-toolchain.md).

## Les six fonctions, et ce qu'elles coûtent

### `IsDebuggerPresent` — rien de perdu

Renvoie toujours `FALSE`. Il n'y a pas de débogueur attaché sur la machine
cible : la réponse est constante *et vraie*. `libstdc++` s'en sert pour décider
d'un `DebugBreak` sur assertion ; elle prendra l'autre branche, qui est la bonne
ici.

### `SetProcessAffinityMask` — rien de perdu

Accepte et ne fait rien. La cible est monoprocesseur (ADR 0002) : il n'existe
qu'un placement possible, et l'accepter est le comportement correct, pas une
approximation.

### `AddVectoredExceptionHandler` / `RemoveVectoredExceptionHandler` — dégradation acceptée

Renvoient `NULL` et `0` — c'est-à-dire « je n'ai pas pu enregistrer ».

Windows 95 n'a que `SetUnhandledExceptionFilter`, qui est un **point unique** et
non une chaîne de gestionnaires. `libgcc` s'en sert de façon facultative et teste
le retour : renvoyer `NULL` est une réponse qu'elle sait traiter.

**Ce qui est perdu** : rien pour `libgcc`, mais toute future utilisation de
gestionnaires vectorisés par le projet échouerait silencieusement. Le mensonge
inverse — renvoyer un jeton non nul — serait pire : le désenregistrement suivant
porterait sur rien.

Le projet installe son propre filtre par `SetUnhandledExceptionFilter`, dans
`platform/win95/startup.c`.

### `GetTickCount64` — une limite réelle, à 49,7 jours

`GetTickCount` revient à zéro après 49,7 jours. La couche accumule les
rebouclages pour rendre un compteur qui, lui, ne revient pas.

**Ce qui est perdu** : la fonction doit être appelée **au moins une fois par
période de 49,7 jours**. Sinon le rebouclage passe inaperçu, et le temps recule
de 49 jours. Ce n'est pas un défaut d'implémentation mais une impossibilité :
deux lectures espacées de 49 jours et de 1 milliseconde sont indiscernables l'une
de l'autre.

Une boucle de jeu tient largement la condition. Un programme qui dormirait plus
longtemps entre deux lectures ne la tiendrait pas.

Le rebouclage est couvert par un test qui le **simule** — attendre sept semaines
n'est pas un protocole. La logique est isolée en fonction pure dans
`platform/win95/tick64.c`, précisément pour être pilotable :

```sh
platform/win95/tests/run-tests.sh tick64
ctest --test-dir build/win95 -R DKRWin95Tick64
```

Le test fige aussi la limite ci-dessus, pour qu'elle reste un choix documenté et
non une surprise.

### Les cinq fonctions de section critique — réimplémentées, pas contournées

`TryEnterCriticalSection` est absente de Windows 95. La couche fournit donc les
**cinq** fonctions — `Initialize`, `Enter`, `TryEnter`, `Leave`, `Delete` — ce
qui lui donne la propriété des 24 octets de `CRITICAL_SECTION` : puisque tout le
binaire passe par elle, leur signification n'appartient qu'à elle.

L'échange atomique passe par `lock cmpxchg`, une instruction du 486, là où
Windows 95 n'exporte pas `InterlockedCompareExchange`. Le processeur sait faire
ce que le système ne propose pas.

**Ce qui est perdu :**

- **Pas de rotation avant blocage.** L'implémentation de Microsoft tourne un
  moment avant de dormir ; celle-ci attend le sémaphore avec un délai de 1 ms.
  Sur une section très disputée et très courte, cela coûte des changements de
  contexte que l'originale évite. Sur un monoprocesseur, la rotation n'a de toute
  façon guère de sens.
- **Attente avec délai plutôt qu'infinie.** Si un réveil se perd entre le test et
  la mise en attente, la boucle le rattrape au tour suivant au lieu de dormir
  pour toujours. C'est un choix de robustesse contre la précision : le réveil
  peut être retardé de 1 ms.
- **Aucun diagnostic.** `DebugInfo` reste nul ; les outils qui inspecteraient la
  structure ne verraient rien.

**Ce qui n'est pas perdu** : la réentrance, la propriété par fil, et l'exclusion
mutuelle. Vérifiées sur la machine — deux fils, 4 000 incréments en contention,
compteur final exactement 4 000.

> **Une leçon qui a coûté cher.** La première version se contentait de renvoyer
> `FALSE` à `TryEnterCriticalSection` — réponse *licite* du contrat, puisque tout
> appelant doit prévoir l'échec, et vérifiée sans danger puisque `try_lock`
> n'apparaît nulle part dans le runtime.
>
> Elle a figé la machine entière. `winpthreads` boucle sur cette fonction pour
> prendre ses verrous, et l'attente active affame l'ordonnanceur de Windows 95
> jusqu'à arrêter l'horloge de la barre des tâches.
>
> **Un bouchon licite n'est pas un bouchon inoffensif.**

### `CreateSemaphoreW` — ajoutée par E02-S01, et d'une autre nature

Les six ci-dessus **manquaient** à la table d'exports, et leur absence est
bruyante : le programme ne démarre pas, et Windows nomme le symbole.
`CreateSemaphoreW` est exportée. Elle ne fait simplement rien — trois
instructions qui rendent zéro et posent `ERROR_CALL_NOT_IMPLEMENTED`, à la même
adresse que `CreateEventW`.

C'est un piège d'une autre classe : le lien réussit, le chargement réussit, le
contrôle des imports était satisfait, et seule l'exécution diffère.

Elle compte parce que `moodycamel::LightweightSemaphore` l'appelle, et que ce
sémaphore est le primitif de blocage de tout le planificateur d'`ultramodern`.
Avec un descripteur nul, l'attente ne bloque plus et le signal boucle sans fin.

La couche la fournit, renvoyée sur `CreateSemaphoreA`, en convertissant le nom
s'il y en a un. **Ce qui est perdu** : rien — `CreateSemaphoreA` est du vrai code,
et la conversion de nom ne peut échouer que sur un nom que la page de codes du
système ne représente pas.

Détail complet et conséquences : [WIN95-THREADING.md](WIN95-THREADING.md) et
[research/win95-blockers.md](research/win95-blockers.md).

## Ce qui n'a pas eu besoin d'être écrit

Le ticket anticipait deux contournements délicats. La mesure les a rendus sans
objet, et c'est un résultat qui mérite d'être consigné :

| API | Statut | Vérification |
|---|---|---|
| `SignalObjectAndWait` | **non réclamée** | absente des imports de `libwinpthread`, `libstdc++` et `libgcc` |
| `InitializeCriticalSectionAndSpinCount` | **non réclamée** | idem |
| `GetThreadId` | **non réclamée** en modèle `posix` | réclamée par le modèle `win32`, écarté par l'ADR 0001 |

Le risque annoncé par le ticket — décomposer `SignalObjectAndWait` en perdant son
atomicité, donc ouvrir une fenêtre de course — **ne se matérialise pas**. Rien ne
la demande.

> **Suite, par E02-S01.** La conclusion « les variables de condition de Vista ne
> sont pas à reproduire » s'est confirmée pour une raison plus forte que le choix
> du modèle `posix` : `ultramodern` n'utilise **aucune** variable de condition.
> Son attente conditionnelle est un sémaphore de comptage.
>
> Le tableau ci-dessus reste vrai, mais il faut lui ajouter une colonne qu'il
> n'avait pas : « exportée » ne veut pas dire « implémentée ». Voir
> [WIN95-THREADING.md](WIN95-THREADING.md).

Les variables de condition de Vista ne sont pas non plus réimplémentées : c'est
précisément ce que le choix du modèle `posix` a permis d'éviter, leur
reproduction sur des événements Windows 95 étant un exercice où l'on perd des
réveils.

## Unicode : les API `...A`, sans exception

Sous Windows 9x, la famille `...W` **est exportée mais ne fait rien**. Le
désassemblage de `KERNEL32.DLL` le montre : chaque entrée `...W` tient en trois
instructions — `xor eax,eax`, un index, un saut vers une queue commune qui pose
`ERROR_CALL_NOT_IMPLEMENTED`. `LoadLibraryExW` et `MoveFileExW` partagent la même
adresse, parce qu'aucune des deux n'a de code.

Conséquence : **toute la couche utilise les API `...A`**, y compris pour les
chemins de fichiers, et le journal de démarrage ouvre son fichier par
`CreateFileA`.

Bonne nouvelle du côté du runtime : `librecomp` travaille en **`std::u8string`**,
c'est-à-dire en UTF-8 sur des octets — 236 usages de chaînes étroites contre 25
de chaînes larges, et ces dernières sont des `u8string`, non des `wchar_t`. Il
n'y a donc pas de conversion large à supprimer.

**Une exception subsiste**, à traiter par E01-S02 : `mod_manifest.cpp:52` appelle
`_wfopen_s`, qui n'est **pas exportée** par le `MSVCRT.DLL` de la machine (seule
`_wfopen` l'est, et elle repose sur `CreateFileW`, donc sur un bouchon). C'est
dans le système de mods, que E00-S01 désigne déjà comme le premier candidat au
fork.

Reste la question des pages de code : UTF-8 n'est pas la page 850 du système. Un
chemin contenant des accents ne sera pas correctement transmis. Ce n'est pas
traité ici, et ce n'est pas urgent — le jeu n'ouvre que des chemins qu'il
fabrique lui-même.

## CRT : liaison statique, sans exception

`-static -static-libgcc -static-libstdc++`. Décision de
[l'ADR 0001](adr/0001-toolchain.md), pour trois raisons dont deux mesurées :

1. **`libgcc_s_dw2-1.dll` n'existe pas sous Windows 95.** Un binaire lié
   dynamiquement à libgcc ne se charge pas — constaté sur un témoin compilé par
   inadvertance sans `-static`.
2. **`MSVCRT.DLL` n'est pas d'origine.** Celle de la machine de test est datée du
   3 novembre 1997, quand tout le reste du système porte le 24 août 1996. Elle
   arrive avec une mise à jour, et **un Windows 95 de première génération ne l'a
   pas du tout**. En dépendre reviendrait à faire dépendre le jeu d'une version
   d'Internet Explorer.
3. La liaison statique supprime toute question de redistribution.

**Conséquence sur la distribution ([E09-S05](stories/E09-qa/E09-S05-packaging-distribution.md))** :
le paquet n'a **aucun redistribuable à embarquer** pour le CRT. Il reste à
vérifier la présence de `glide2x.dll`, fournie par le pilote de la carte et non
par le paquet.

Le coût est la taille du binaire : 501 Ko pour un témoin qui en ferait 51 avec
Open Watcom. Sans objet au regard des 14 Mio de marge de
[l'ADR 0003](adr/0003-budget-memoire.md).

## Démarrage

`dkr_win95_startup()` doit être appelée en première ligne de `main`. Elle fait
trois choses que personne d'autre ne fera :

**Un journal dans un fichier.** Il n'y a pas de console utilisable sur la machine
cible : un jeu plein écran qui meurt avant son premier affichage ne laisse rien à
lire. Le journal s'écrit **à côté de l'exécutable** — lancé depuis le menu
Démarrer, un programme hérite d'un répertoire courant qui n'a rien à voir avec
l'endroit où l'utilisateur ira chercher le fichier — et il est **vidé après
chaque ligne**, de sorte que la dernière ligne survive au plantage qui l'a
interrompue. C'est justement celle-là qui compte.

**Un filtre d'exceptions structurées.** Sans lui, une instruction invalide produit
une boîte de dialogue qui ne nomme rien d'exploitable. Avec lui, le code et
l'adresse partent dans le journal. Le cas `EXCEPTION_ILLEGAL_INSTRUCTION` porte
une note explicite : sur cette cible, c'est le symptôme d'une instruction
postérieure au Pentium II qui aurait échappé au contrôle de E01-S01.

**Un contrôle de version.** Win32s sur Windows 3.1 et toute version antérieure à
4.0 sont refusés par un message compréhensible, plutôt que par un plantage sur
une API absente. Windows NT est accepté : le binaire y tourne aussi, ce qui rend
le développement moins pénible.

Relevé sur la machine de test :

```
=== journal de demarrage ===
Temoin plate-forme
D:\DKR-BOOT.LOG
filtre d'exceptions installe
systeme : plate-forme 1, version 4.0 build 1111
 C
demarrage termine
```

Plate-forme 1 est `VER_PLATFORM_WIN32_WINDOWS`, et la version 4.0 build 1111
avec le marqueur « C » est la signature de Windows 95 OSR2.

## Vérifier

```sh
./Build-Win95.sh
ctest --test-dir build/win95                        # tick64 + fils (E02-S01)
scripts/Push-To-Win95-VM.sh build/win95/bin/PLATFORM.EXE
```

Le témoin `PLATFORM.EXE` exerce la couche entière. Sur la machine de test :

```
IsDebuggerPresent      : faux
SetProcessAffinityMask : accepte
GetTickCount64         : 123 ms ecoulees        (pour un Sleep de 120 ms)
TryEnterCriticalSection: verrou libre pris
deux fils, 4000 tours  : compteur = 4000 / 4000
```
