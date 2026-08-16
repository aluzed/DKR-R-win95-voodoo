# E02-S05 — Sauvegardes : EEPROM et Controller Pak

| | |
|---|---|
| **Épic** | E02 — Substrat système Windows 95 |
| **Statut** | IN_PROGRESS |
| **Priorité** | P1 |
| **Estimation** | S |
| **Dépend de** | E02-S03 |
| **Bloque** | E09-S03 |

## Contexte

DKR sauvegarde sa progression en EEPROM, et le jeu gère quatre Controller Pak
virtuels. DKR-R implémente tout cela dans du code appartenant au projet —
`virtual_pak.cpp` (24 Ko), `save_manager.cpp` (25 Ko), `dkr_save_codec.cpp`
(17 Ko) — et non dans une dépendance. C'est du C++ portable dont la logique n'a
aucune raison de changer.

Ce qui change relève entièrement de l'écriture de fichiers :

- l'emplacement des fichiers de sauvegarde — il n'y a pas de `%APPDATA%` sous
  Windows 95, et le dossier de l'application est l'usage de l'époque ;
- les noms de fichiers — le système de fichiers peut être FAT16 en 8.3 ;
- la robustesse à l'écriture — sur cette classe de machine, l'extinction brutale
  pendant une écriture est un scénario courant, pas un cas limite.

Le gestionnaire de sauvegardes graphique (`save_manager`, T.T.'s Save Manager)
dépend d'ImGui et disparaît avec lui (E07-S02). Le codec, lui, reste.

## Objectif

Faire fonctionner les sauvegardes EEPROM et Controller Pak sous Windows 95, en
conservant les formats de fichiers compatibles avec ceux de DKR-R.

## Périmètre

**Dans :** stockage, emplacement, robustesse, compatibilité de format.

**Hors :** l'interface graphique de gestion des sauvegardes (E07-S02).

## Travail

1. Relever comment `librecomp` et `virtual_pak.cpp` déterminent l'emplacement des
   fichiers, et introduire un chemin adapté à Windows 95 : dossier de
   l'application par défaut, remplaçable par la configuration (E06-S05).
2. Vérifier que tous les noms de fichiers produits tiennent en 8.3, ou que le
   comportement sur FAT16 est vérifié plutôt que supposé.
3. Rendre l'écriture atomique : écriture dans un fichier temporaire, vidage des
   tampons, puis remplacement. Vérifier que le remplacement se comporte comme
   attendu sous Windows 95 — `MoveFileEx` avec remplacement n'y est pas
   disponible, la séquence doit donc être écrite à la main.
4. Conserver le format de fichier de DKR-R, de sorte qu'une sauvegarde soit
   transportable entre la version moderne et la version Win95. C'est aussi ce qui
   permettra de préparer un état de jeu sur l'hôte moderne pour tester une
   situation précise sur la cible — un gain de temps réel en QA.
5. Vérifier les 18 suites de tests existantes qui portent sur la sauvegarde
   (`dkr_save_codec_tests.cpp`, `save_manager_tests.cpp`) et les faire tourner sur
   la cible.
6. Traiter la sauvegarde de secours : conserver la copie précédente, de sorte
   qu'une écriture interrompue ne détruise pas une progression.
7. Vérifier le comportement en écriture sur disque plein et sur support en lecture
   seule — le message doit être compréhensible, pas un plantage.

## Critères d'acceptation

- [x] Les sauvegardes EEPROM fonctionnent sous Windows 95 : écriture, relecture,
      persistance après redémarrage. Les deux premières par
      `save_manager_tests` sur la machine ; la troisième par une sauvegarde
      écrite dans une session, la machine arrêtée proprement, puis relue et
      réencodée octet pour octet dans la session suivante.
- [x] Les quatre Controller Pak virtuels fonctionnent, autotest inclus. Relevé
      du 14 août 2026, `DKRR.EXE --self-test-pak` sur la machine :

      ```text
      [boot][pak] recovered controller pak 4 from backup
      [test][pak] PASS: round-trip and backup recovery
      ```

      Le contrôle porte sur le **vrai binaire du jeu**, pas sur une suite
      séparée, et il exerce la reprise depuis la copie de secours — c'est-à-dire
      la séquence d'écriture durable elle-même.
- [x] Les noms de fichiers sont compatibles 8.3, ou le comportement FAT16 est
      vérifié — les deux : les noms longs fonctionnent sur ce volume, et les noms
      que la couche fabrique tiennent en 8.3 pour rester utilisables ailleurs.
- [x] L'écriture est **aussi atomique que Windows 95 le permet** — il n'y a pas
      de remplacement atomique — et une copie de secours est conservée. Ce que la
      séquence garantit et ce qu'elle ne garantit pas est écrit, et **la coupure
      est désormais provoquée plutôt que simulée** : `kill -9` sur l'émulateur,
      après plus de 350 tours d'écriture.

      ScanDisk n'a trouvé qu'un fichier endommagé — `COUPURE.TMP`, celui que la
      séquence sacrifie — et la sauvegarde du tour 370 a été relue intacte après
      redémarrage, sans même recourir à la copie de secours.

      Ce relevé ne prouve pas que la fenêtre ne soit jamais atteinte : c'est un
      tirage, et la coupure est tombée pendant l'écriture du `.TMP`, qui occupe
      l'essentiel de chaque tour. Il prouve que la séquence tient sous une
      coupure réelle, et que le volume se répare au démarrage suivant.
- [x] Une sauvegarde produite par DKR-R moderne est lue par la version Win95, et
      réciproquement — et le résultat est plus fort que demandé : **les deux
      constructions produisent les mêmes octets**.

      | | SHA-256 |
      |---|---|
      | écrite par la construction hôte | `2673ca1a…4dae47bc` |
      | écrite sur Windows 95 | `2673ca1a…4dae47bc` |

      Chacune relit celle de l'autre, décode et réencode sans un octet d'écart.
      Le témoin (`tools/win95/witnesses/save_interchange.cpp`) pose des motifs
      **asymétriques** dans les champs de 16 et 32 bits — `0x1234` et non
      `0x1221` — parce qu'une inversion d'octets sur une valeur symétrique ne se
      voit pas, et que c'est le risque principal quand la même structure est
      encodée par deux compilateurs différents.
- [x] `dkr_save_codec_tests` et `save_manager_tests` passent sur la cible —
      relevé du 13 août 2026, les quatre phases puis PASS. Il aura fallu
      lever trois obstacles que seule l'exécution révélait : `<fstream>`
      inchargeable, les flux ouverts sur un `path` qui passent par l'API
      large, et `MoveFileExW` appelée directement. Voir
      [docs/research/win95-wide-streams.md](../../research/win95-wide-streams.md).
- [x] Les codes sont distingués, portent un texte, et **les deux cas limites sont
      provoqués sur la machine** — support absent (erreur 21) et disque plein.

      Le second demandait un volume qu'on puisse remplir : le disque de transfert
      a un demi-gigaoctet de libre, d'où une disquette de 1,44 Mo remplie à
      l'avance depuis l'hôte et montée en A:.

      ```text
      cible              : A:\PLEIN.DAT
      taille demandee    : 2097152 octets
      code rendu         : 4
      texte              : disque plein
      verdict            : DISQUE PLEIN, correctement nomme
      ```

      L'écriture visée est plus grande que **le volume entier**, et non seulement
      que l'espace restant : une écriture qui tiendrait tout juste ne prouverait
      rien de reproductible, la place libre dépendant de ce qui traîne sur le
      support.

## État au 2026-08-13 — la couche d'écriture est livrée et mesurée

`platform/win95/fileio.{h,cpp}`, avec sa suite `test_fileio.cpp` (hôte **et**
`FILEIOT.EXE`). Relevé : [`docs/research/win95-fileio.md`](../../research/win95-fileio.md).

**Le ticket avait raison sur `MoveFileEx`** — à la différence de deux
suppositions de E02-S03, démenties par la mesure. Mais la forme de son
indisponibilité échappe aux deux garde-fous du dépôt : `MoveFileExA` est
exportée, elle a du **vrai code**, et elle refuse quand même avec
`ERROR_CALL_NOT_IMPLEMENTED`. Ni le contrôle d'imports ni le relevé des bouchons
ne pouvaient la voir. C'est une **troisième catégorie**, que seule l'exécution
révèle, et elle est consignée dans `tools/win95/exports/stubs/PROVENANCE.md`.

La séquence d'écriture est donc manuelle, et sa fenêtre assumée. **La garantie
offerte n'est pas « on ne perd jamais la dernière écriture » mais « on ne perd
jamais une sauvegarde valide »** : perdre la dernière course est désagréable,
perdre la progression entière ne se pardonne pas. `.TMP` n'est jamais relu, parce
que rien ne prouve qu'il soit complet et que le format de DKR-R ne porte pas de
somme de contrôle.

| | |
|---|---|
| Suite sur l'hôte | 40 contrôles, 0 échec, propre sous ASan |
| Suite sur la cible | **40 contrôles, 0 échec** |
| Noms longs sur FAT16 | fonctionnent ; l'alias 8.3 tronque l'extension |
| Écriture refusée | erreur 21 `ERROR_NOT_READY`, distincte et exploitable |

Les coupures ne sont pas attendues mais **simulées**, en fabriquant à la main les
états intermédiaires que la séquence traverse — comme le rebouclage de E01-S03.

### `<filesystem>` : la règle du dépôt était fausse, et coûteuse

Ce dépôt bannissait `<filesystem>` en bloc, en lui attribuant treize symboles
absents. La mesure dit autre chose — relevé complet dans
[`docs/research/win95-filesystem.md`](../../research/win95-filesystem.md) :

| | Symboles bloquants | Se charge ? |
|---|---:|---|
| `#include <filesystem>` seul | **0** | oui |
| un objet `std::filesystem::path` | 1, un **bouchon** | **oui** |
| un appel à `exists()` | 17, dont **7 absents** | **non** |

Et `path` ne fait pas que se charger : il **fonctionne**, vérifié sur la machine —
construction, `parent_path`, `filename`, `extension`, concaténation, tout est
juste. C'est de la manipulation de chaînes, et la manipulation de chaînes ne
demande rien au système.

L'écart de coût entre la règle et la réalité est de deux ordres de grandeur :

| | Occurrences | À faire |
|---|---:|---|
| `std::filesystem::path` — le **type** | **250** | **rien** |
| les opérations | ~140 | à router vers `fileio.h` |

Bannir l'en-tête aurait fait réécrire 250 usages pour un gain nul, **et** laissé
croire le problème résolu tant que subsistaient les 140 qui comptent. Le
contrôleur de sous-ensemble surveille désormais les opérations, et elles seules,
avec un auto-test qui l'éprouve dans les deux sens.

Effet immédiat : **`ultramodern` n'a plus aucune inclusion interdite**, et son
cliquet a été retiré — le contrôleur l'a signalé de lui-même.

### Le point d'indirection est livré

`platform/win95/fileio.hpp` détourne les quatre opérations qu'emploie le cœur de
`librecomp` — `exists`, `remove`, `create_directories`, `copy_file` — **sans
toucher au type** : les signatures gardent `std::filesystem::path`, et les 250
usages du type restent intacts. Une suite unique éprouve l'équivalence des deux
branches, `std::filesystem` sur l'hôte et les API `...A` sur la cible :
**12 contrôles, 0 échec des deux côtés**.

L'un d'eux mérite d'être cité, parce qu'il n'est pas évident : `remove` rend
**false** sur un fichier déjà absent, sans que ce soit une erreur. L'appelant
voulait qu'il ne soit plus là, il ne l'est pas ; mais rien n'a été effacé, et
`std::filesystem::remove` le dit ainsi. Un point d'indirection qui rendrait true
aurait l'air correct et ferait mentir tout code comptant les fichiers supprimés.

Deux pièges rencontrés en le câblant sont consignés dans le relevé : un **faux
positif du vérificateur de jeu d'instructions**, qui désassemblait les tables
d'exceptions logées dans `.text` — corrigé — et le fait que
**`std::random_device` ne fonctionne pas sur cette cible**, son chemin passant
par un bouchon.

### Le cœur de `librecomp` est câblé

Patch **0018** : les dix appels d'opérations hors système de mods passent par un
point d'indirection, `recomp::fs`, que la cible remplit avec la couche de ce
ticket. Le type n'est pas touché.

| | |
|---|---|
| Opérations routées dans le cœur de `librecomp` | **10 sur 10** |
| `librecomp` sur l'hôte 64 bits | 26 unités sur 26 |
| Suites de la cible moderne | 18 sur 18 |
| Suites de la cible Win95 | 4 sur 4 |

`copy_options` n'entre délibérément pas dans le point d'indirection :
`overwrite_existing` est la seule forme employée, donc elle est **nommée** plutôt
que paramétrée. Un point d'indirection qui reproduit une option que personne ne
passe sera faux le jour où quelqu'un la passe.

Ce qui reste dans `librecomp` est **le système de mods** — 12 signalements, avec
quatre opérations de plus dont `directory_iterator` — que
[E00-S01](../../research/win95-blockers.md) désigne déjà comme la partie dont ce
portage n'a pas besoin. Et deux `<mutex>` dans `recomp.cpp` et `pi.cpp`, qui
relèvent de E02-S02.

## Risques

Perdre la progression d'un joueur est le défaut le moins pardonnable d'un
portage. L'atomicité et la copie de secours ne sont pas du confort : sur une
machine de 1998 sans onduleur, l'écriture interrompue arrivera.

## Références

- `runtime-recomp/src/game/virtual_pak.cpp`, `save_manager.cpp`,
  `dkr_save_codec.cpp`
- `runtime-recomp/tests/dkr_save_codec_tests.cpp`, `save_manager_tests.cpp`
- `Build-Linux.sh:36` — autotest de Controller Pak
