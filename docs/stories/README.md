# Portage DKR-R vers Windows 95 + 3dfx Voodoo

Backlog de développement. Chaque ticket est un fichier autonome sous
`docs/stories/<epic>/`.

## Objectif du projet

Faire tourner DKR-R — la recompilation statique de Diddy Kong Racing — sous
Windows 95, rendu par une carte 3dfx via Glide. Le joueur fournit sa propre
ROM ; aucun asset n'est redistribué (`docs/ASSET_POLICY.md`).

**Cible retenue** : Pentium II / III, Voodoo 2 ou 3, 64 Mo de RAM, Windows 95
OSR2.5. Ce plancher est provisoire jusqu'à ce que [E00-S05](E00-cadrage/E00-S05-adr-cible-materielle-glide.md)
l'acte sur les mesures de E00-S03 et E00-S04.

## Ce qui est conservé, ce qui tombe

La valeur de ce dépôt est dans son code généré et dans son décodeur F3DDKR. Le
reste de la pile est spécifique aux systèmes modernes.

| Couche | Devenir |
|---|---|
| Sortie N64Recomp (`RecompiledFuncs`) | **conservée** — du C portable manipulant des entiers |
| Microcode audio recompilé (`aspMain`) | **conservé**, émulation vectorielle réécrite sans SSE (E03) |
| Décodeur F3DDKR (`f3ddkr_rt64.cpp`) | **extrait** de RT64, reposé sur une interface propre (E04) |
| Codec de sauvegarde, Controller Pak, politique audio | **conservés** — code portable du projet |
| `ultramodern` / `librecomp` | **patchés** — primitives système substituées (E02) |
| RT64 | **remplacé** par un backend Glide (E05) |
| SDL2 | **remplacé** par du Win32 brut (E06) |
| Dear ImGui, texture packs, overlay CRT | **retirés** (E07) |
| Mode Moderne, interpolation, identités de présentation | **retirés** (E07-S01) |

## Statut global

| Épic | Titre | Tickets | TODO | En cours |
|---|---|---|---|---|
| [E00](E00-cadrage/) | Cadrage, mesures et décisions | 7 | 5 | **2** |
| [E01](E01-build/) | Chaîne de build 32 bits Win95 | 6 | 5 | **1** |
| [E02](E02-systeme/) | Substrat système Win95 | 6 | 2 | **4** |
| [E03](E03-rsp/) | RSP sur x86 sans SSE | 3 | 3 | 0 |
| [E04](E04-hle-f3ddkr/) | HLE F3DDKR indépendant de RT64 | 8 | 8 | 0 |
| [E05](E05-glide/) | Backend Glide | 8 | 8 | 0 |
| [E06](E06-plateforme/) | Plateforme Win95 | 6 | 6 | 0 |
| [E07](E07-perimetre/) | Réduction de périmètre | 3 | 3 | 0 |
| [E08](E08-perf/) | Performance | 4 | 4 | 0 |
| [E09](E09-qa/) | Intégration, QA et distribution | 5 | 4 | **1** |
| | **Total** | **56** | **49** | **7** |

### En cours

| Ticket | État |
|---|---|
| [E09-S01](E09-qa/E09-S01-environnement-test-emule.md) | `REVIEW` — **environnement complet** : Windows 95 OSR2.5 sur Pentium II / Voodoo 2, pilote 3dfx installé, **démonstration Glide rendant un triangle Gouraud**, instantané de référence figé, machine pilotable sans écran. |
| [E00-S04](E00-cadrage/E00-S04-spike-cout-rsp-sans-sse.md) | `REVIEW` — **le microcode audio recompilé ne peut pas tenir le temps réel** : la cible n'atteint que **3,9 %** du débit vectoriel du RSP. Le repli scalaire existait déjà ; MMX ne sauverait pas ce chemin. **[E03-S03](E03-rsp/E03-S03-repli-mixeur-haut-niveau.md) passe de contingence à chemin critique.** |
| [E01-S05](E01-build/E01-S05-compilation-code-recompile.md) | `IN_PROGRESS` — **le code recompilé compile et se lie pour Windows 95** : 37 fichiers sur 37, un PE de 4,23 Mo qui passe les deux garde-fous, `aspMain.cpp` compris et sans bouchon. Aucune extension absente — l'arithmétique 64 bits passe par libgcc. La comparaison à l'oracle **concorde bit à bit sur les fonctions atteintes**, fautes comprises ; elle s'interrompt sur une faute que Windows 95 ne délivre pas comme signal. **`librecomp` compile également** — 26 unités sur 26 — les « six erreurs » de E01-S02 se réduisant à un seul `static_assert` plus des chemins d'inclusion, et `allocation_size`, qui valait **zéro** en 32 bits, suit désormais la cible. |
| [E02-S05](E02-systeme/E02-S05-sauvegardes-eeprom-controller-pak.md) | `IN_PROGRESS` — couche d'écriture durable livrée, **40 contrôles sans échec sur la cible**, les coupures étant simulées plutôt qu'attendues. Le ticket avait raison sur `MoveFileEx`, mais sa forme d'indisponibilité révèle une **troisième catégorie d'API absente** : exportée, avec du vrai code, et refusant à l'exécution — que ni le contrôle d'imports ni le relevé des bouchons ne peuvent voir. |
| [E02-S03](E02-systeme/E02-S03-horloge-timers-cadence.md) | `IN_PROGRESS` — base de temps livrée et mesurée : `QueryPerformanceCounter` à **1 193 180 Hz, soit le PIT 8254**, 4,19 µs, 200 000 lectures sans un recul, et une **dérive de −0,0000 % sur 300 s** sur la cible. Deux suppositions du ticket sont démenties (`GetTickCount` est à 9 ms et cent fois moins chère ; `timeBeginPeriod(1)` ne change rien ici). Défaut trouvé en chemin : `ultramodern` dérive `osGetCount` de `high_resolution_clock`, qui est **l'horloge murale** sur cette chaîne — le brancher sur cette base est donc justifié par la mesure. |
| [E02-S02](E02-systeme/E02-S02-ordonnanceur-ultramodern.md) | `IN_PROGRESS` — patch 0015 : les cinq primitives d'`ultramodern` passent par un point d'indirection que la cible remplit avec la couche de E02-S01. **`ultramodern` compile pour Windows 95, 15 fichiers sur 15**, cibles modernes inchangées, inclusions interdites de **9 à 1**. `thread_local` fonctionne sur la cible, mesuré. Les points 5 à 7 restent bloqués par E01-S05, E02-S05 et E07-S03. |
| [E02-S01](E02-systeme/E02-S01-couche-threads-synchronisation.md) | `REVIEW` — couche de fils complète : fils, verrous, sémaphore, **variable de condition**, événements, TLS. **48 contrôles sans échec sous Windows 95 émulé**, endurance de 600 s (8 437 tours). Deux bloquants invisibles trouvés : `CreateSemaphoreW` et `GetHandleInformation` sont exportées par Windows 95 mais **vides**, ce qui casse le sémaphore de `moodycamel` et `std::thread::join()` ; le garde-fou des imports contrôle désormais aussi les exports vides. Le relevé initial, fait sur un worktree que `apply-dependency-patches.sh` — cassé — laissait sans ses treize premiers patchs, avait conclu à tort qu'aucune variable de condition n'était nécessaire. |
| [E00-S03](E00-cadrage/E00-S03-spike-budget-cpu-recompilation.md) | Les deux facteurs sont mesurés : **2,16×** pour le passage 64 → 32 bits sans SSE, **17,7×** pour la normalisation vers le Pentium II 400 MHz — soit **≈ 38×** entre le poste de développement et la cible. Go/no-go non prononcé : il manque désormais le coût CPU d'une image de jeu, qui exige [E02-S06](E02-systeme/E02-S06-amorcage-jeu.md). |

### Acquis en chemin

| Livrable | Portée |
|---|---|
| [`docs/research/cpu-budget.md`](../research/cpu-budget.md) | Mesures de E00-S03, méthode et limites |
| [`docs/research/rsp-audio-budget.md`](../research/rsp-audio-budget.md) | Mesures de E00-S04 — le chiffre qui déclenche E03-S03 |
| [`docs/TEST-ENVIRONMENT.md`](../TEST-ENVIRONMENT.md) | Recette de l'environnement émulé et ses limites connues |
| `scripts/Setup-Win95-Toolchain.sh` | Toolchain MIPS, cmake, ninja, uv — **sans droits root** |
| `scripts/generate_recomp_toml.py` | Configuration N64Recomp depuis la politique, portage Linux du script PowerShell — avance [E01-S06](E01-build/E01-S06-generation-sources-hors-windows.md) |
| `scripts/Setup-Win95-TestVM.sh`, `prepare_win95_install.py`, `Run-Win95-VM.sh`, `Drive-Win95-VM.sh`, `Push-To-Win95-VM.sh` | Machine de test : montage, préparation de l'installation depuis l'ISO du joueur, lancement, pilotage sans écran, transfert de fichiers |
| `tools/win95/azerty_keys.py` | Traduit un texte en touches physiques pour un invité AZERTY — sans quoi aucun chemin de fichier n'est saisissable |
| `scripts/patch_voodoo2_inf.py` | 86Box expose sa Voodoo 2 avec l'identifiant PCI de la Voodoo 1 ; le pilote d'origine ne la reconnaît pas sans cette correction |
| `tools/win95/glidetest.c` + `build-glidetest.sh` | Démonstration Glide — et **premier PE 32 bits sans CRT ni SSE tournant sous Windows 95**, ce qui avance [E00-S02](E00-cadrage/E00-S02-spike-toolchain-pe-win95.md) |
| `tools/cpu-budget/` | Banc d'essai rejouable du code recompilé, **sur l'hôte et sur la machine cible** |
| `patches/n64recomp/0002-…` | Multiplication 64×64→128 portable : **débloque toute la cible 32 bits** |

**L'oracle du projet est constructible sous Linux.** L'ELF du decomp de référence
se bâtit désormais sans Windows, et la ROM produite est identique au bit près à
celle du joueur — le decomp voisin donnait pourtant sa toolchain MIPS pour
absente.

## Ordre d'attaque

Deux tickets se font **avant tout le reste**, et pour des raisons opposées.

[**E09-S01**](E09-qa/E09-S01-environnement-test-emule.md) — l'environnement de
test émulé. Il est classé en QA par thème, mais c'est un prérequis pratique :
sans machine Windows 95 restaurable en quelques secondes, tout le reste se
développe à l'aveugle. Il conditionne même E00-S02.

[**E00-S03**](E00-cadrage/E00-S03-spike-budget-cpu-recompilation.md) — le budget
CPU. Il décide si le projet est faisable sur cette classe de machine, et il porte
un **go / no-go** explicite. Le découvrir maintenant coûte une semaine ; le
découvrir après E04 et E05 en coûte trois mois.

```
E09-S01 (machine de test)
   │
E00 (cadrage) ── E00-S03 : GO / NO-GO ──┐
   │                                     │ si no-go : plancher relevé,
   │                                     │ ou bascule vers le portage natif
   ├──> E01 (build 32 bits) ──> E02 (substrat système)
   │                                │
   │                          E02-S06 : le jeu tourne sous Win95
   │                                │   (sans image — renderer de diagnostic)
   │                                │
   ├──> E07 (réduction de périmètre, en parallèle)
   │                                │
   │            E04 (HLE F3DDKR) ───┤
   │                  │             │
   │            E04-S08 (rastériseur logiciel)
   │                  │
   │            [PREMIÈRE IMAGE]
   │                  │
   │            E05 (backend Glide) <── E09-S02 (comparaison visuelle)
   │                  │
   ├──> E03 (RSP) ──> E06 (plateforme Win95)
   │                  │
   │            [JEU JOUABLE]
   │                  │
   └──> E08 (performance) ──> E09 (validation, packaging)
```

E03 (RSP / audio) est largement indépendant et peut être mené en parallèle.

### Les trois jalons vérifiables

| Jalon | Ticket | Ce qu'il prouve |
|---|---|---|
| Le jeu s'exécute | [E02-S06](E02-systeme/E02-S06-amorcage-jeu.md) | Build, substrat système et ordonnanceur tiennent. Pas d'image, mais des tâches graphiques soumises à cadence mesurable. |
| La première image | [E04-S08](E04-hle-f3ddkr/E04-S08-rasteriseur-logiciel-reference.md) | Le décodeur F3DDKR est juste, indépendamment de Glide. Devient l'oracle de E05. |
| Le jeu est jouable | E05 + E06 | Rendu accéléré, entrées, audio, cadence. |

## Le point dur

[**E05-S03**](E05-glide/E05-S03-traduction-color-combiner.md) — la traduction du
combineur de couleurs. Le combineur du RDP est programmable ; celui de Glide est
fixe. C'est le seul ticket estimé XL de la partie graphique.

Ce qui le rend traitable : DKR n'utilise que **33 configurations**, dont **3
seulement lisent deux texels** — inventaire déjà établi par le portage natif
voisin (`../../Diddy-Kong-Racing/docs/research/combiner-inventory.md`), à
revérifier par [E04-S06](E04-hle-f3ddkr/E04-S06-etat-rdp.md). Trente-trois cas
énumérés, dont on connaît la fréquence et la surface d'écran : un problème fini.

C'est aussi pourquoi E04-S08 est un prérequis et non un confort. Sans oracle
implémentant le combineur fidèlement, E05-S03 se fait à l'appréciation, et
l'erreur se cumule sans être imputable.

## Répartition d'effort estimée

| Domaine | Part |
|---|---|
| HLE F3DDKR + backend Glide (E04, E05) | ~35 % |
| Build, substrat système, RSP (E01, E02, E03) | ~30 % |
| Plateforme et réduction de périmètre (E06, E07) | ~15 % |
| Cadrage, performance, QA (E00, E08, E09) | ~20 % |

## Contraintes non négociables

1. **Jamais de modification directe des worktrees de dépendances.** `ultramodern`,
   `librecomp`, `N64Recomp`, RT64, `RecompiledFuncs` et `RecompiledPatches` sont
   régénérés — une édition directe disparaît sans prévenir. Tout passe par
   `patches/manifest.json` (`docs/ARCHITECTURE.md`).
2. **Aucun asset redistribué.** La ROM vient du joueur ; le paquet est scanné
   avant distribution.
3. **32 bits, sans SSE.** Deux garde-fous automatiques l'imposent : vérification
   du jeu d'instructions ([E01-S01](E01-build/E01-S01-toolchain-cmake-i686-sans-sse.md))
   et vérification des imports PE ([E01-S04](E01-build/E01-S04-garde-fou-imports-pe.md)).
4. **L'oracle reste constructible.** La cible moderne est l'unique référence
   exécutable permettant de savoir si le portage est *juste*. Sa conservation est
   tranchée par [E00-S07](E00-cadrage/E00-S07-adr-strategie-branche-oracle.md) ;
   la casser sans nécessité, c'est perdre le moyen de vérifier.

## Ce que le dépôt voisin apporte déjà

`/var/www/Diddy-Kong-Racing` porte un portage **natif** du même jeu vers la même
cible, depuis le decomp plutôt que par recompilation statique. Son backlog
« Voodoo95 » a plusieurs livrables directement réutilisables ici :

| Livrable | Utilisé par |
|---|---|
| `docs/research/combiner-inventory.md` — 33 configurations, 3 à deux texels | E04-S06, E05-S03, E05-S04 |
| `docs/research/level-working-set.md` — pic de 1,20 Mo par niveau | E00-S05, E05-S02 |
| Défauts du C `NON_MATCHING` du decomp | E08-S02 |
| Arbitrages d'architecture (fork, oracle, rastériseur de référence) | E00-S07, E04-S08 |

C'est aussi le **repli** si E00-S03 conclut au no-go : son approche ne porte pas
le surcoût de traduction du code recompilé, au prix d'un travail bien plus lourd
sur le reste.

## Conventions

### Statuts

| Statut | Signification |
|---|---|
| `TODO` | Pas commencé |
| `IN_PROGRESS` | En cours |
| `BLOCKED` | Bloqué — raison et ticket bloquant notés dans le ticket |
| `REVIEW` | Implémenté, en attente de validation |
| `DONE` | Validé contre ses critères d'acceptation |

Le statut se met à jour **dans le fichier du ticket** (champ `Statut`) **et** dans
le tableau ci-dessus. Un ticket passe en `DONE` uniquement quand tous ses critères
d'acceptation sont cochés.

### Priorités

- **P0** — chemin critique, bloque d'autres épics
- **P1** — nécessaire au jeu jouable
- **P2** — qualité, confort, optimisation
- **P3** — optionnel

### Estimations

`S` ≤ 1 jour · `M` 2–4 jours · `L` 1–2 semaines · `XL` > 2 semaines

## Références transverses

- `docs/ARCHITECTURE.md` — chemin d'exécution et frontières protégées
- `docs/F3DDKR.md` — pont microcode → renderer
- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md` — instantané RDRAM et identités
- `docs/BUILDING.md`, `docs/ASSET_POLICY.md`
- [Sources Glide 3dfx](https://sourceforge.net/projects/glide/) ·
  [sezero/glide](https://github.com/sezero/glide) ·
  [hatarch/glide3x](https://github.com/hatarch/glide3x)
- PCem · 86Box — émulation de machines d'époque avec cartes 3dfx
