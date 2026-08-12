# E01-S05 — Compilation du code recompilé généré

| | |
|---|---|
| **Épic** | E01 — Chaîne de build 32 bits Windows 95 |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E01-S01, E01-S02, E00-S06 |
| **Bloque** | E02-S06, E08-S02 |

## Contexte

C'est le cœur du portage : les milliers de fonctions C produites par N64Recomp à
partir de l'ELF du decomp, plus le microcode audio recompilé. Sur l'hôte moderne,
elles compilent sans histoire. En 32 bits, trois questions restent ouvertes :

1. **Le langage.** Le C généré manipule les registres du VR4300 comme des entiers
   de 64 bits. En 32 bits, chaque opération devient une paire — le compilateur le
   fait, mais il faut vérifier qu'il ne s'appuie pas sur une extension absente.
2. **Le volume.** Le nombre de fichiers générés est de l'ordre du millier, et
   `runtime-recomp/CMakeLists.txt` les regroupe en une bibliothèque statique
   unique. Temps de compilation, mémoire de l'éditeur de liens et taille du
   binaire final sont tous inconnus sur cette cible.
3. **L'espace d'adressage.** `librecomp` réserve la RDRAM par une allocation
   contiguë. En 32 bits sous Windows 95, l'espace utilisateur est d'environ 2 Go,
   fragmenté, et la réservation doit réussir de façon fiable au lancement.

## Objectif

Compiler `DKRRecompiledFuncs` et `RecompiledRSP` pour la cible Win95, mesurer ce
que ça coûte, et rendre le résultat chargeable dans le budget de E00-S06.

## Périmètre

**Dans :** compilation, édition de liens, mesure de taille et de temps,
réservation de la RDRAM.

**Hors :** l'optimisation des performances d'exécution (E08-S02) et l'émulation
vectorielle du RSP (E03-S01).

## Travail

1. Générer les sources par le chemin existant (`Build-DKR-Runtime.cmd` puis
   `Generate-DKR-RSP.cmd`) et relever le nombre de fichiers et le volume de C
   produit. E01-S06 traite séparément la question de la génération hors Windows.
2. Compiler `DKRRecompiledFuncs` avec la toolchain de E01-S01. Traiter les erreurs
   par famille : le code généré est répétitif, une erreur unique se répète des
   milliers de fois et se corrige d'un coup.
3. Vérifier le comportement de l'arithmétique 64 bits générée : opérations sur
   registres, décalages, multiplication et division, extension de signe. Un test
   ciblé sur quelques fonctions arithmétiques du jeu, comparé à la sortie de
   l'oracle 64 bits, vaut mieux qu'une relecture.
4. Compiler `RecompiledRSP/aspMain.cpp`. Il inclut `librecomp/rsp_vu_impl.hpp` :
   si l'émulation vectorielle ne compile pas sans SSE, poser un bouchon qui
   compile, laisser le comportement à E03-S01, et le noter comme dette explicite.
5. Mesurer et consigner : temps de compilation complet, pic de mémoire de
   l'éditeur de liens, taille des sections `.text` et `.rdata`, taille du binaire.
6. Si l'édition de liens échoue par épuisement mémoire ou par dépassement d'une
   limite de format, découper en plusieurs bibliothèques statiques, ou évaluer une
   DLL séparée pour le code généré. Le découpage doit rester automatique — un
   découpage manuel ne survit pas à la prochaine régénération.
7. Vérifier que la réservation de la RDRAM réussit sous Windows 95 : quelle
   quantité, à quelle adresse, et quel est le comportement en cas d'échec.
8. Confronter la taille obtenue au budget mémoire de E00-S06 et remonter l'écart.

## Critères d'acceptation

- [ ] `DKRRecompiledFuncs` compile intégralement pour la cible Win95.
- [ ] `RecompiledRSP/aspMain.cpp` compile, l'émulation vectorielle étant soit
      fonctionnelle, soit un bouchon nommément renvoyé à E03-S01.
- [ ] L'arithmétique 64 bits générée est vérifiée par comparaison à l'oracle sur
      un échantillon de fonctions.
- [ ] Temps de compilation, pic mémoire du lien et taille des sections sont
      mesurés et consignés.
- [ ] L'édition de liens produit un PE 32 bits qui passe les deux garde-fous
      (E01-S01, E01-S04).
- [ ] La réservation de RDRAM est vérifiée sous Windows 95, avec son comportement
      d'échec.
- [ ] La taille obtenue est confrontée au budget de E00-S06, écart remonté.

## Risques

Si le binaire dépasse largement la mémoire physique disponible, Windows 95
paginera pendant la course, et la pagination sur un disque de 1998 rend le jeu
injouable indépendamment de toute optimisation CPU. Ce chiffre doit donc être
mesuré tôt : il peut à lui seul rouvrir l'ADR de plancher matériel (E00-S05).

## Références

- `runtime-recomp/CMakeLists.txt:80-120` — bibliothèque `DKRRecompiledFuncs`
- `runtime-recomp/CMakeLists.txt:122-130` — sources RSP générées
- `docs/BUILDING.md` — chemin de génération
- `Generate-DKR-RSP.cmd`, `Diagnose-DKR-Recompile.cmd`
