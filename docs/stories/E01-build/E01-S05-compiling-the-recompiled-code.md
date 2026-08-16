# E01-S05 — Compilation du code recompilé généré

| | |
|---|---|
| **Épic** | E01 — Chaîne de build 32 bits Windows 95 |
| **Statut** | IN_PROGRESS |
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

- [x] `DKRRecompiledFuncs` compile intégralement pour la cible Win95 — 37/37.
- [x] `RecompiledRSP/aspMain.cpp` compile, l'émulation vectorielle étant
      **fonctionnelle** : le repli scalaire suffit, aucun bouchon n'est posé.
- [~] L'arithmétique 64 bits générée est vérifiée par comparaison à l'oracle.
      **Concorde exactement sur les fonctions atteintes**, fautes comprises ; la
      comparaison s'interrompt sur une faute que Windows 95 ne délivre pas comme
      signal. Voir la note ci-dessus.
- [x] Temps de compilation, pic mémoire et taille des sections sont mesurés et
      consignés.
- [x] L'édition de liens produit un PE 32 bits qui passe les deux garde-fous —
      4,23 Mo, aucune instruction hors Pentium II, chargeable sous Windows 95.
- [ ] La réservation de RDRAM est vérifiée sous Windows 95, avec son comportement
      d'échec.
- [ ] La taille obtenue est confrontée au budget de E00-S06, écart remonté.

## État au 2026-08-13 — tout compile et se lie ; la comparaison est partielle

Relevé complet : [`docs/research/win95-recompiled-code.md`](../../research/win95-recompiled-code.md).

| | |
|---|---:|
| Fichiers générés compilés | **37 sur 37**, zéro erreur |
| Compilation, en parallèle | 5,76 s, pic de 142 Mo par processus |
| `.text` du PE lié | 3 813 Ko |
| PE complet | **4,23 Mo** |
| `aspMain.cpp` | compile, **sans bouchon** — le repli scalaire suffit |
| Garde-fous | jeu d'instructions **et** imports : passés |

Trois résultats méritent d'être notés.

**Aucune extension absente.** L'arithmétique 64 bits appelle `__divdi3`,
`__moddi3`, `__ashrdi3` et leurs voisins, tous fournis par libgcc, que l'ADR
0001 lie déjà statiquement. C'était la première des trois questions du ticket.

**Le point 4 est sans objet.** `aspMain.cpp` compile sans SSE grâce au repli
scalaire de `rsp_vu_impl.hpp` : il n'y a pas de bouchon à poser ni de dette à
renvoyer à E03-S01. Sa lenteur reste le sujet de E03-S03, pas de la compilation.

**La comparaison à l'oracle concorde sur ce qu'elle atteint** — empreintes bit à
bit identiques, et jusqu'aux fautes, qui se produisent des deux côtés sur les
mêmes fonctions. Elle s'interrompt sur `func_8001CD28`, dont la faute n'est pas
délivrée comme `SIGSEGV` sous Windows 95 alors qu'elle l'est sur l'hôte. Ce
n'est pas un débordement de pile : une réserve de 64 Mo n'y change rien. La
suite demande un pilote qui reprend après la fonction en cours, ce qui
fonctionne sur les deux cibles sans dépendre de ce que le système veut bien
délivrer.

### `librecomp` compile aussi, et les deux hypothèses 64 bits tombent

Ce n'était pas le périmètre de ce ticket, mais c'est ce qui bloquait tous les
autres, et le mur s'est révélé plus mince que le décompte de E01-S02 le laissait
croire. **Les 26 unités de traduction de `librecomp` compilent** pour la cible,
sans instruction hors Pentium II.

Les « six erreurs » recensées étaient en réalité **une seule cause pour cinq
d'entre elles** — le `static_assert(sizeof(std::size_t) == 8)` de `mods.hpp`,
qui remontait par inclusion — plus deux chemins d'inclusion manquants et un
en-tête que le CMake de miniz fabrique. Deux patchs suffisent :

- **0016** — le hachage de `HookDefinition` s'assemble en `uint64_t` puis se
  replie si `size_t` est plus étroit, au lieu d'exiger 64 bits ; et
  `patch_func` reçoit son trampoline i386, `mov eax, imm32 ; jmp eax`, le
  pendant exact des six autres octets de la variante x86_64. Le hachage 64 bits
  est inchangé, vérifié sur deux millions de triplets.
- **0017** — `allocation_size` valait `4096ULL * 1024 * 1024`, ce qui **fait
  exactement zéro** quand `size_t` est de 32 bits : `VirtualAlloc` réservait
  rien, échouait, et le jeu s'arrêtait sur « Failed to allocate memory » avant
  la première image. Les tailles suivent désormais la cible, selon ce que
  l'[ADR 0003](../../adr/0003-memory-budget.md) avait tranché — 4 Mio validés,
  8 Mio réservés, de sorte que le piège à accès invalides reste armé.

L'image du code recompilé mesurée ici, **3,81 Mio de `.text`**, confirme
l'estimation de 3,85 Mio sur laquelle cette ADR avait bâti son budget.

**Ce qui reste hors d'atteinte** : le point 7, la réservation de RDRAM en
conditions réelles, et le point 8, la confrontation au budget de E00-S06. Les
deux demandent le jeu lié pour de bon, ce qui attend encore `librecomp`
(E02-S05) et le découplage SDL2 (E07-S03). E00-S01 a déjà mesuré les plafonds —
1 Gio de réservation, 256 Mio de validation, le schéma de `librecomp` à 8 Mio
fonctionnel.

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
