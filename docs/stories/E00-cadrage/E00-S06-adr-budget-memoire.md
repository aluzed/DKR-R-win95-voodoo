# E00-S06 — ADR : budget mémoire sur une machine de 64 Mo

| | |
|---|---|
| **Épic** | E00 — Cadrage, mesures et décisions |
| **Statut** | REVIEW |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E00-S01 |
| **Bloque** | E01-S05, E02-S04, E08-S04 |

## État au 2026-08-12 — ADR écrite

[`docs/adr/0003-budget-memoire.md`](../../adr/0003-budget-memoire.md).

| Poste | Décision | Budget |
|---|---|---:|
| RDRAM validée | **4 Mio** — borne réelle du pool de DKR (`RAM_END`, `EXPANSION_PAK_SUPPORT 0`) | 4 Mio |
| Espace réservé | 8 Mio + garde, au lieu de 4 Gio | 8 Mio |
| Instantané RDRAM | **4 Mio, une seule tâche en vol** | 4 Mio |
| Code recompilé | **mesuré** : 3,85 Mio, contre 10-40 Mio estimés | 3,85 Mio |
| ROM | **lecture à la demande** au lieu de 12 Mio résidents | 0,06 Mio |
| Pile 3dfx | **mesurée** : 872 Kio seulement | 0,85 Mio |
| Réserves runtime + textures | déclarées, à vérifier | 12 Mio |
| **Total / disponible mesuré** | | **32,8 / 47,0 Mio** |

Deux mesures ont porté les décisions :

- **la mémoire disponible pilote 3dfx actif est de 47,0 Mio**, relevée sur la
  machine par `tools/win95/probes/glide_memory.c`. Windows 95 en consomme 15,6 ;
  la pile Glide, seulement 872 Kio — les tampons 640×480 vivent dans la mémoire
  de la carte, pas dans celle de la machine ;
- **le code recompilé fait 3,85 Mio**, mesuré sur les 37 objets de E00-S03. Le
  ticket l'estimait entre 10 et 40 Mo.

Une découverte au passage : la file d'instantanés est une
`BlockingConcurrentQueue` **non bornée**. Sur une cible où le rendu est plus lent
que la production de tâches, elle croît jusqu'à l'épuisement de la mémoire — un
plantage à retardement, inoffensif sur machine moderne.

**La cible 32 Mo est écartée** : il n'y resterait que ~16 Mio, sans marge. 64 Mo
est confirmé comme plancher matériel.

**Non fait, et assumé** : le pic mémoire de la build actuelle n'est pas mesuré —
aucune build de la cible n'existe (E01-S05), et profiler la build moderne
mesurerait précisément les postes qui disparaissent. Le budget est construit par
le bas. Le critère d'acceptation correspondant reste ouvert jusqu'à E01-S05.

## État antérieur — les plafonds mesurés

[E00-S01](E00-S01-inventaire-dependances-incompatibles.md) a mesuré sur la
machine cible ce que le code source ne pouvait pas dire :

| Grandeur | Mesure |
|---|---:|
| RAM physique / disponible | 63 Mio / 47 Mio |
| Espace d'adressage virtuel | 2 044 Mio |
| Granularité d'allocation | 65 536 |
| **Réservation maximale** | **1 024 Mio** |
| **Validation en lecture-écriture maximale** | **256 Mio** |
| Schéma réserve+valide+protège de `librecomp` à 8 Mio | **OK** |

Et un défaut à corriger, dans `librecomp/include/librecomp/addresses.hpp` :

- `allocation_size = 4096ULL * 1024ULL * 1024ULL` **vaut 0** une fois tronqué
  dans un `size_t` de 32 bits. GCC ne le signale que par un avertissement ; le
  jeu compile, se lie, puis meurt sur « Failed to allocate memory ».
- `mem_size = 512 Mio` **échouerait aussi** même sans la troncature : le plafond
  de validation mesuré est de 256 Mio.

Le mécanisme de `librecomp` n'est pas en cause — il fonctionne à 8 Mio, soit
exactement la RDRAM d'une N64 avec Expansion Pak. L'ADR porte donc sur le choix
des deux constantes selon la cible, pas sur un changement d'approche.

## Contexte

Le runtime moderne dépense la mémoire sans compter, parce qu'il n'a aucune raison
de compter. Sous Windows 95 avec 64 Mo, chaque poste devient un arbitrage.

Les postes connus :

| Poste | Taille | Origine |
|---|---|---|
| RDRAM émulée | 4 Mo (8 Mo si Expansion Pak) | `librecomp` |
| Instantané RDRAM par tâche graphique | **8 Mo par tâche en attente** | `patches/n64-modern-runtime/0007-snapshot-rdram-for-queued-graphics-tasks.patch` |
| File de charges de travail RT64 | 4 emplacements | RT64 — disparaît avec RT64 |
| Image du code recompilé | à mesurer, probablement 10 à 40 Mo | E01-S05 |
| Textures décodées côté hôte | à mesurer | E04-S07 |
| Image de la ROM | 12 Mo si chargée entièrement | `librecomp` |

Deux postes sautent aux yeux. L'instantané de 8 Mo par tâche graphique en attente
est calibré pour une machine où la mémoire est gratuite ; DKR est un jeu de 4 Mo
de RDRAM, l'instantané peut vraisemblablement suivre. Et charger les 12 Mo de ROM
en mémoire est un luxe quand `librecomp` pourrait la lire par morceaux.

## Objectif

Écrire `docs/adr/0003-budget-memoire.md` : le budget par poste, la somme, et la
marge restante sur une machine de 64 Mo — puis les décisions de réduction qui en
découlent.

## Périmètre

**Dans :** l'inventaire chiffré et les décisions de réduction.

**Hors :** leur mise en œuvre (E08-S04 pour l'instantané, E02-S04 pour la ROM).

## Travail

1. Mesurer l'empreinte réelle de la build actuelle en fonctionnement : pic de tas,
   pic de mémoire engagée, taille des sections du binaire. Un profileur d'allocation
   sur l'hôte moderne suffit — les tailles ne dépendent pas de l'OS cible.
2. Ventiler le pic par poste selon le tableau ci-dessus, sans laisser de reliquat
   non attribué supérieur à 10 %.
3. Décider de la taille de l'instantané RDRAM. Vérifier la taille de RDRAM que DKR
   utilise effectivement — le jeu n'exige pas l'Expansion Pak — et si l'instantané
   peut descendre à 4 Mo, voire être remplacé par une copie du seul segment lu par
   la display list.
4. Décider du nombre de tâches graphiques simultanément en vol. Sur cette cible, une
   seule en vol est probablement le bon compromis : la file profonde sert
   l'interpolation moderne, qui disparaît avec le profil « Accurate » (E07-S01).
5. Décider du mode d'accès à la ROM : image complète en mémoire, ou lecture par
   morceaux à la demande (E02-S04).
6. Poser un plafond global et le comparer à ce qui reste réellement disponible sous
   Windows 95 avec 64 Mo, une fois l'OS et le pilote 3dfx chargés. Mesurer cette
   disponibilité dans la machine de test plutôt que de l'estimer.
7. Écrire l'ADR avec le budget par poste, la somme, la marge, et les décisions.

## Critères d'acceptation

- [ ] Le pic mémoire de la build actuelle est mesuré et ventilé par poste, reliquat
      non attribué sous 10 %.
- [ ] La mémoire réellement disponible sous Win95 / 64 Mo, pilote 3dfx chargé, est
      mesurée et non estimée.
- [ ] `docs/adr/0003-budget-memoire.md` fixe un plafond par poste et un plafond
      global, avec la marge restante.
- [ ] La taille de l'instantané RDRAM et le nombre de tâches en vol sont tranchés
      et justifiés.
- [ ] Le mode d'accès à la ROM est tranché.
- [ ] Une configuration de repli à 32 Mo est évaluée : ce qui tombe, et si le jeu
      reste jouable.

## Risques

Windows 95 pagine sur disque, et un disque de 1998 rend la pagination
catastrophique en cours de course. Le budget doit tenir en mémoire physique avec
marge, pas seulement en mémoire virtuelle : un budget qui « tient » à 63 Mo sur
64 est un budget faux.

## Références

- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md` — l'instantané de 8 Mio et son rôle
- `patches/n64-modern-runtime/0007-snapshot-rdram-for-queued-graphics-tasks.patch`
- `runtime-recomp/src/game/renderer_snapshot.hpp`
