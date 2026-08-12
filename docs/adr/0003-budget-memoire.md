# ADR 0003 — Budget mémoire sur une machine de 64 Mo

- **Statut** : accepté
- **Date** : 2026-08-12
- **Ticket** : [E00-S06](../stories/E00-cadrage/E00-S06-adr-budget-memoire.md)

## Contexte

Le runtime moderne dépense la mémoire sans compter, parce qu'il n'a aucune raison
de compter : il réserve 4 Gio d'espace d'adressage et en valide 512 Mio. Sur la
machine cible, chaque poste devient un arbitrage — et Windows 95 pagine sur un
disque de 1998, ce qui rend tout dépassement catastrophique en course.

## Ce qui est mesuré

### Mémoire réellement disponible, pilote 3dfx actif

Relevé sur la machine de test par `tools/win95/probes/glide_memory.c`, qui
interroge `GlobalMemoryStatus` à quatre moments :

| Moment | Physique libre | Coût |
|---|---:|---:|
| Au repos, bureau chargé | 49 028 Kio | — |
| `glide2x.dll` chargée | 48 880 Kio | 148 Kio |
| Contexte Glide ouvert, 640×480, double tampon + Z | **48 156 Kio** | 724 Kio |
| Contexte fermé | 48 292 Kio | (136 Kio non rendus) |

Physique total : 65 012 Kio. **Windows 95 et ses pilotes en consomment 15 984 Kio
au repos**, soit 15,6 Mio.

Enseignement contre-intuitif : **la pile 3dfx ne coûte que 872 Kio de RAM
système**. Les tampons de 640×480 en 16 bits — image, image arrière, profondeur,
soit 1,76 Mio — vivent dans la mémoire de la carte, pas dans celle de la machine.
La Voodoo n'entame donc quasiment pas le budget.

**Plafond de travail retenu : 47 Mio**, sur la foi de la troisième ligne.

### Image du code recompilé

Le ticket estimait « probablement 10 à 40 Mo ». La mesure, faite sur les 37
objets 32 bits produits par [E00-S03](../research/cpu-budget.md) — soit
l'intégralité du code du jeu généré :

| Section | Taille |
|---|---:|
| `.text` | 3 896 223 o (3,72 Mio) |
| `.eh_frame` | 100 456 o |
| `.rodata` (toutes variantes) | 37 085 o |
| **Total chargeable** | **4 037 386 o (3,85 Mio)** |

L'estimation était haute d'un ordre de grandeur. C'est **3,85 Mio**, et ce poste
cesse d'être une inquiétude.

### RDRAM réellement utilisée par DKR

`librecomp` déclare 8 Mio au jeu (`recomp.cpp:510`). Le jeu, lui, n'en veut pas
tant. Dans le decomp (`src/memory.h:22-23`, `include/config.h:7`) :

```c
#define RAM_END           0x80400000   /* 4 Mio */
#define EXPANSION_RAM_END 0x80800000   /* 8 Mio */
#define EXPANSION_PAK_SUPPORT 0        /* le jeu n'utilise pas l'Expansion Pak */
```

`mempool_init_main` prend donc `RAM_END` : **le pool principal de DKR s'arrête à
4 Mio**. Une seule adresse de la carte de liens dépasse cette borne,
`assets_VRAM_END` à `0x80b09df0` — mais `asset_loading.c` transfère les assets
depuis la ROM par DMA à la demande ; c'est une adresse de segment ROM, pas de la
RAM occupée.

## Décision

### Budget par poste

| Poste | Aujourd'hui | Décision | Budget |
|---|---:|---|---:|
| Espace réservé | 4 Gio (→ **0** en 32 bits) | réserver 8 Mio + garde | 8 Mio |
| RDRAM validée | 512 Mio | **4 Mio**, borne réelle du jeu | 4 Mio |
| Instantané RDRAM par tâche | 8 Mio × file non bornée | **4 Mio × 1 en vol** | 4 Mio |
| Image du code recompilé | — | mesurée | 3,85 Mio |
| Image de la ROM | 12 Mio résidents | **lecture à la demande** | 0,06 Mio |
| File de charges RT64 | 4 emplacements | disparaît avec RT64 | 0 |
| Pile 3dfx | — | mesurée | 0,85 Mio |
| Runtime, CRT, tas C++ | — | **réserve** | 4 Mio |
| Textures décodées côté hôte | — | **réserve**, à mesurer en E04-S07 | 8 Mio |
| **Total** | | | **32,8 Mio** |
| **Disponible mesuré** | | | **47,0 Mio** |
| **Marge** | | | **14,2 Mio (30 %)** |

La marge est délibérément large. Un budget qui « tient » à 46 Mio sur 47 est un
budget faux : la première allocation imprévue le fait paginer, et une course qui
pagine n'est pas jouable.

### 1. Corriger les deux constantes de `librecomp`

`librecomp/include/librecomp/addresses.hpp` :

```cpp
constexpr size_t mem_size        =  512ULL * 1024ULL * 1024ULL;
constexpr size_t allocation_size = 4096ULL * 1024ULL * 1024ULL;   // vaut 0 en 32 bits
```

Ces valeurs deviennent dépendantes de la cible : **`mem_size` = 4 Mio**,
`allocation_size` = 8 Mio, ce qui laisse une région protégée de 4 Mio au-dessus
de la RDRAM pour piéger les accès invalides — le mécanisme de `librecomp` reste
intact, et [E00-S01](../research/win95-blockers.md) a vérifié qu'il fonctionne à
cette taille sur la machine réelle.

`osMemSize` reste déclaré à 8 Mio ou passe à 4 : **à trancher en E02-S04**, après
vérification que rien dans DKR ne lit cette valeur pour dimensionner autre chose
que le pool. C'est le seul point de cette ADR qui reste ouvert, et il est
délibérément laissé ouvert plutôt que tranché sans preuve.

### 2. Instantané RDRAM : 4 Mio, une seule tâche en vol

Le patch `0007-snapshot-rdram-for-queued-graphics-tasks` copie `0x800000`
octets — 8 Mio — par tâche graphique mise en file, dans une
`moodycamel::BlockingConcurrentQueue` **qui n'est pas bornée**.

Deux corrections, pour deux raisons distinctes :

- **la taille passe à 4 Mio**, parce que c'est la borne du pool de DKR : copier
  au-delà copie de la mémoire que le jeu n'écrit jamais ;
- **une seule tâche en vol**, parce que la file profonde sert l'interpolation
  d'images de RT64, qui disparaît avec le profil « Accurate »
  ([E07-S01](../stories/E07-perimetre/E07-S01-profil-accurate-seul.md)).

Le second point est le plus important, et ce n'est pas qu'une question de
budget : sur la cible, **le rendu sera plus lent que la production de tâches**.
Une file non bornée devant un consommateur plus lent que le producteur ne
plafonne pas — elle croît jusqu'à l'épuisement de la mémoire. Ce qui est
inoffensif sur une machine moderne devient un plantage à retardement ici.

À terme, l'instantané devrait disparaître au profit d'une copie du seul segment
lu par la display list — mais cela demande de connaître ce segment, ce qui relève
de [E04-S02](../stories/E04-hle-f3ddkr/E04-S02-parseur-display-list-autonome.md).
Ce n'est pas une décision de cette ADR.

### 3. ROM : lecture à la demande

`librecomp/src/pi.cpp:14` garde la ROM entière en mémoire :

```cpp
static std::vector<uint8_t> rom;
```

et le DMA PI y lit par `memcpy` (lignes 70 et 80). **12 Mio résidents pour deux
sites de lecture.**

Décision : remplacer par une poignée de fichier et un tampon de transfert, avec
un cache de 64 Kio. Les deux sites sont les seuls à convertir, et DKR est déjà
écrit pour ce modèle — il transfère ses assets par DMA asynchrone plutôt que de
supposer la ROM présente. Mise en œuvre en
[E02-S04](../stories/E02-systeme/E02-S04-acces-rom-dma-pi.md).

Le disque de la machine cible est lent, et c'est le risque de cette décision :
si le DMA à la demande provoque des à-coups en course, le repli est de charger
en mémoire les seules zones chaudes. À mesurer en E02-S04, pas à supposer ici.

### 4. Configuration de repli à 32 Mo

Windows 95 consomme 15,6 Mio au repos, mesurés. Sur une machine de 32 Mo il
resterait donc de l'ordre de **16 Mio** — contre 32,8 Mio de budget.

**La cible 32 Mo n'est pas tenue, et n'est pas retenue.** Ce qui pourrait
éventuellement y entrer, en supprimant toute réserve de textures et l'instantané :
RDRAM 4 + code 3,85 + Glide 0,85 + runtime 4 ≈ 12,7 Mio, laissant 3 Mio pour les
textures décodées. C'est un budget sans marge, sur une machine qui paginerait au
moindre écart.

**64 Mo est donc le plancher matériel du projet**, ce qui confirme
la cible matérielle retenue ([E00-S05](../stories/E00-cadrage/E00-S05-adr-cible-materielle-glide.md), ADR 0002 encore à écrire) plutôt
que de l'assouplir.

## Ce qui n'est pas mesuré, et pourquoi

Le ticket demandait de mesurer le **pic mémoire de la build actuelle en
fonctionnement**, ventilé par poste avec moins de 10 % de reliquat. **Ce n'est
pas fait**, et ce n'est pas faisable aujourd'hui :

- aucune build de la cible n'existe encore — c'est
  [E01-S05](../stories/E01-build/E01-S05-compilation-code-recompile.md) ;
- profiler la build moderne mesurerait RT64, ImGui, SDL2 et les texture packs,
  c'est-à-dire précisément les postes qui disparaissent. Le chiffre serait exact
  et sans rapport avec la question.

Le budget ci-dessus est donc construit **par le bas**, poste par poste, à partir
de valeurs mesurées quand elles existaient (disponible cible, coût Glide, taille
du code, borne du pool de DKR) et de réserves déclarées quand elles n'existaient
pas (textures, tas du runtime).

**Conséquence à tenir** : dès que E01-S05 produit un exécutable, le pic réel doit
être mesuré et confronté à ce tableau. Les deux réserves — 4 Mio de runtime et
8 Mio de textures — sont les postes à vérifier en priorité, car ce sont les seuls
qui ne reposent sur aucune mesure.

## Conséquences

- **E02-S04** hérite de deux décisions : ROM lue à la demande, et l'arbitrage sur
  `osMemSize`.
- **E08-S04** hérite de l'instantané : 4 Mio, une tâche en vol.
- **E04-S07** doit mesurer l'empreinte des textures décodées contre la réserve de
  8 Mio.
- **E01-S05** doit mesurer le pic réel et le confronter à ce tableau.
- Le plancher matériel reste **64 Mo**, confirmé par la mesure et non assoupli.

## Références

- `tools/win95/probes/glide_memory.c` — la mesure sur cible
- [`docs/research/win95-blockers.md`](../research/win95-blockers.md) — troncature de `allocation_size`, plafonds `VirtualAlloc`
- [`docs/research/cpu-budget.md`](../research/cpu-budget.md) — les 37 objets mesurés
- `extern/n64-modern-runtime/librecomp/include/librecomp/addresses.hpp:10-12`
- `extern/n64-modern-runtime/librecomp/src/pi.cpp:14,70,80`
- `patches/n64-modern-runtime/0007-snapshot-rdram-for-queued-graphics-tasks.patch`
- `../../Diddy-Kong-Racing/src/memory.h:22-23`, `include/config.h:7` — bornes du pool de DKR
