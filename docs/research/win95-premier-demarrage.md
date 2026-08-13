# Le jeu démarre sous Windows 95

Relevé du 13 août 2026, sur la machine de test — Pentium II 400 MHz,
Windows 95 OSR2.

```text
[boot][input] keyboard: WASD=stick arrows=d-pad Space=A Shift=B Z=Z Enter=Start
                        IJKL=C Q=L E=R
The diagnostic runtime requires a ROM path.
```

Le programme se charge, initialise sa couche d'entrée, affiche sa configuration
clavier et s'arrête sur le message prévu : aucune ROM ne lui a été fournie. Ce
n'est pas un plantage — c'est le chemin nominal jusqu'au point où il manque une
donnée.

## Ce que l'édition de liens a coûté

55 unités de traduction — 37 fichiers recompilés, le microcode RSP, les 17
sources du jeu — plus le recompilateur à la volée et ses dépendances.

| | Symboles réclamés |
|---|---:|
| première tentative | 30 |
| o1heap, absent de la construction | −3 |
| miniz, idem — **et quatre fichiers, pas un** | −6 |
| DbgHelp, que Windows 95 n'a pas | −8 |
| recompilateur à la volée, N64Recomp et rabbitizer | −13 |

Puis le contrôle des imports en a trouvé cinq autres, invisibles au lieur parce
qu'il les résolvait depuis les bibliothèques d'import de mingw : `_wfopen_s`,
`_wfreopen_s`, `_strtoi64`, `_strtoui64`, `GetModuleHandleExW`.

**Aucun symbole non résolu ne venait de la couche plate-forme, d'`ultramodern`,
des sources du jeu ni du code recompilé.** C'était la question que cette édition
de liens posait, et la réponse est la meilleure possible.

## Trois pièges, tous de la même famille

### `strtoll` cache `_strtoi64`

`_strtoi64` a d'abord été écrite en appelant `strtoll`. Sous mingw, `strtoll`
**est** un renvoi vers `_strtoi64` importé de MSVCRT : la définition était
circulaire. Elle compilait, se liait, et le symbole absent réapparaissait dans
la table d'imports sans que rien ne le signale.

L'analyse est donc écrite à la main, et `strtoll`/`strtoull` sont fournies
par-dessus — sans quoi `mod_manifest.cpp`, qui les appelle, ramenait l'import par
la porte de service.

### `std::thread` sans `#include <thread>`

Le contrôleur de sous-ensemble lisait les inclusions. `librecomp` n'inclut
`<thread>` nulle part directement — il arrive par transitivité — et `recomp.cpp`
construisait pourtant **le fil du jeu** avec `std::thread`. Rien n'a protesté.

Le binaire s'est chargé et est mort au démarrage :

```text
terminate called after throwing an instance of 'std::system_error'
  what():  Resource temporarily unavailable
```

C'est `pthread_create` qui échoue derrière la bibliothèque standard — la même
cause que le bouchon `GetHandleInformation` documenté depuis E02-S01.

**Un contrôle qui lit les inclusions ne peut pas voir un usage.** C'est
exactement la leçon que `<filesystem>` avait déjà donnée, où la surveillance
porte sur les opérations et non sur l'en-tête. Le contrôleur surveille désormais
les types eux-mêmes.

### Le pont de fils n'acceptait pas les pointeurs sur membre

Son constructeur variadique appelait directement, avec ce commentaire :
« aucun appel d'`ultramodern` n'est un pointeur sur membre ». C'était vrai
d'`ultramodern`. `librecomp` démarre un fil sur
`&ModContext::dirty_mod_configuration_thread_process`, et la forme directe ne
compile pas pour lui.

`std::invoke` est ce qu'emploie `std::thread`, et c'est le contrat qu'il fallait
reproduire. La supposition d'origine économisait un en-tête — `<functional>` est
de la bibliothèque pure et n'ajoute aucun import — au prix d'une divergence de
contrat. Le mauvais côté du marché.

## Ce qui reste bouchonné, et ce que cela coûte

Six symboles sont exportés et vides. Aucun n'empêche le chargement ; chacun est
justifié dans `tools/win95/exports/exceptions.json`.

| Symbole | Origine | Conséquence |
|---|---|---|
| `GetProcessTimes`, `GetThreadTimes`, `GetSystemTimeAdjustment` | `clock.o` de libwinpthread, tiré par `<chrono>` | aucune — seuls les identifiants d'horloge processeur y mènent, et personne ne les demande |
| `MoveFileExW` | `fs_ops.o` de libstdc++ | aucune — plus aucun code n'appelle `std::filesystem::rename` ici |
| `LoadLibraryExW` | `mods.cpp` | les mods natifs ne se chargent pas — hors périmètre |
| `WriteConsoleW` | `fmt`, via `N64Recomp` | les diagnostics du recompilateur à la volée n'apparaissent pas |

`CreateProcessW` en faisait partie : le redémarrage rapide aurait échoué en
silence. Il est passé en `CreateProcessA`, qui fait la même chose partout.

## Ce que cela ne prouve pas

Le jeu **démarre**. Il n'a pas encore tourné avec une ROM, et rien de ce qui
suit le chargement n'est éprouvé : ni la boucle de jeu, ni le rendu Voodoo, ni
le son, ni la cadence. Le recompilateur à la volée est lié et n'a jamais été
exécuté sur cette machine — il alloue une page et y écrit du code, ce qui reste
à voir sous Windows 95.
