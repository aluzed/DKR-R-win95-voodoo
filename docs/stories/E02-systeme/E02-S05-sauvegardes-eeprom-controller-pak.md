# E02-S05 — Sauvegardes : EEPROM et Controller Pak

| | |
|---|---|
| **Épic** | E02 — Substrat système Windows 95 |
| **Statut** | TODO |
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

- [ ] Les sauvegardes EEPROM fonctionnent sous Windows 95 : écriture, relecture,
      persistance après redémarrage.
- [ ] Les quatre Controller Pak virtuels fonctionnent, autotest inclus
      (`--self-test-pak` existe déjà dans les scripts de build).
- [ ] Les noms de fichiers sont compatibles 8.3, ou le comportement FAT16 est
      vérifié.
- [ ] L'écriture est atomique et une copie de secours est conservée.
- [ ] Une sauvegarde produite par DKR-R moderne est lue par la version Win95, et
      réciproquement.
- [ ] `dkr_save_codec_tests` et `save_manager_tests` passent sur la cible.
- [ ] Disque plein et support en lecture seule produisent un message clair.

## Risques

Perdre la progression d'un joueur est le défaut le moins pardonnable d'un
portage. L'atomicité et la copie de secours ne sont pas du confort : sur une
machine de 1998 sans onduleur, l'écriture interrompue arrivera.

## Références

- `runtime-recomp/src/game/virtual_pak.cpp`, `save_manager.cpp`,
  `dkr_save_codec.cpp`
- `runtime-recomp/tests/dkr_save_codec_tests.cpp`, `save_manager_tests.cpp`
- `Build-Linux.sh:36` — autotest de Controller Pak
