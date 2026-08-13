# E02-S03 — Horloge, minuteries et base de temps

| | |
|---|---|
| **Épic** | E02 — Substrat système Windows 95 |
| **Statut** | IN_PROGRESS |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E02-S01 |
| **Bloque** | E02-S05, E06-S04, E08-S01 |

## Contexte

DKR mesure le temps par le compteur de cycles du VR4300, à 46,875 MHz — la moitié
de la fréquence du processeur. `ultramodern` traduit ce compteur vers une horloge
hôte, et toute la simulation en dépend : cadence des images, chronométrage des
courses, temporisation de l'audio.

Sous Windows 95, les sources de temps disponibles sont inégales :

| Source | Résolution | Remarque |
|---|---|---|
| `GetTickCount` | ~55 ms par défaut | trop grossière, et déborde après 49,7 jours |
| `timeGetTime` (`winmm`) | 1 ms avec `timeBeginPeriod` | correcte, coût système à mesurer |
| `QueryPerformanceCounter` | dépend du matériel | présente dès Windows 95, à valider sur la cible |
| `RDTSC` | cycle | Pentium et suivants ; fréquence à calibrer, et sensible aux modes d'économie d'énergie |

`QueryPerformanceCounter` est le bon candidat par défaut, mais son comportement
exact sur du matériel de 1998 se vérifie plutôt qu'il ne se suppose — la
fréquence retournée par `QueryPerformanceFrequency` varie selon le chipset.

## Objectif

Livrer une base de temps monotone, de résolution suffisante pour un chronométrage
à 30 images par seconde, et la brancher sur le compteur de cycles émulé
d'`ultramodern`.

## Périmètre

**Dans :** la source de temps, sa calibration, sa validation, et les minuteries.

**Hors :** la synchronisation d'affichage (E06-S04) et le rythme audio (E06-S03),
qui consomment cette base sans la définir.

## Travail

1. Écrire `platform/win95/clock.{h,cpp}` avec sélection de la source au lancement :
   `QueryPerformanceCounter` si elle est disponible et cohérente, repli sur
   `timeGetTime` avec `timeBeginPeriod(1)`.
2. Valider la source retenue au démarrage : monotonie stricte, résolution
   effective mesurée, absence de saut. Une source incohérente doit être écartée au
   profit du repli plutôt que provoquer un comportement erratique en jeu.
3. Traiter le débordement pour toute source 32 bits, par accumulation en 64 bits.
   Le test doit simuler le passage à zéro — c'est le genre de défaut qui ne se
   rencontre jamais en développement et toujours chez un joueur.
4. Brancher le compteur de cycles du VR4300 sur cette base. Vérifier que le
   rapport est exact : le compteur avance à 46,875 MHz, indépendamment de la
   fréquence de l'hôte.
5. Implémenter les minuteries dont `ultramodern` a besoin (l'équivalent de
   `osSetTimer` et de la file de minuteries), sur la couche d'attente de E02-S01.
6. Mesurer le coût d'un appel à la source retenue. Il est consulté plusieurs fois
   par image ; sur un Pentium II, un appel système coûteux répété devient un poste
   de budget à part entière. Consigner le chiffre pour E08-S01.
7. Vérifier que `timeBeginPeriod(1)`, s'il est utilisé, est bien relâché à
   l'arrêt : sous Windows 9x, un réglage laissé en place dégrade tout le système
   jusqu'au redémarrage.

## Critères d'acceptation

- [x] `platform/win95/clock.{h,cpp}` sélectionne et valide sa source au lancement —
      une source qui recule, même d'un pas, est écartée au profit du repli.
- [x] La résolution effective est mesurée sous Windows 95 émulé et consignée.
- [x] Le débordement 32 bits est traité et couvert par un test qui simule le
      passage à zéro — par `dkr_tick64_step`, reprise de E01-S03 plutôt que
      recopiée.
- [x] Le compteur de cycles émulé avance à 46,875 MHz, vérifié sur une durée
      longue plutôt que sur un instant — **300 s sur la cible, −0,0000 %**.
- [ ] Les minuteries d'`ultramodern` fonctionnent, avec leur précision mesurée.
      Bloqué : elles ne s'exercent qu'en faisant tourner le jeu.
- [x] Le coût d'un appel est mesuré et consigné, avec la réserve que
      l'émulation n'est pas temporelle et que E09-S04 doit le confirmer.
- [~] `timeBeginPeriod` est relâché par `dkr_clock_shutdown`. Le cas de l'arrêt
      **anormal** reste à couvrir : il demande de le relâcher depuis le filtre
      d'exceptions de E01-S03.

## État au 2026-08-13 — la base est livrée et mesurée

`platform/win95/clock.{h,cpp}`, avec sa suite `test_clock.cpp` (hôte **et**
`CLOCKT.EXE`). Relevé complet : [`docs/research/win95-clock.md`](../../research/win95-clock.md).

Le tableau des sources du ticket a été remplacé par des mesures, et **deux de ses
suppositions étaient fausses** :

| Source | Supposé | Mesuré |
|---|---|---|
| `GetTickCount` | ~55 ms | **9 ms**, et cent fois moins chère que les autres |
| `timeGetTime` | 1 ms *avec* `timeBeginPeriod` | 1 ms **sans** — le réglage ne change rien ici |
| `QueryPerformanceCounter` | « dépend du matériel » | 1 193 180 Hz, soit le **PIT 8254** ; 4,19 µs |

La fréquence dit d'où vient le compteur, et il en découle un fait qu'il vaut
mieux connaître avant qu'après : **les 32 bits de poids faible du PIT rebouclent
en exactement 60 minutes**.

Résultats sur la cible :

| | |
|---|---|
| Source retenue | `QueryPerformanceCounter`, 1 193 180 Hz |
| Monotonie | 200 000 lectures, **0 recul** |
| Dérive du compteur VR4300 sur **300 s** | **−0,0000 %** (536 cycles sur 14,06 milliards) |
| Suite | 20 contrôles, 0 échec |

### Un défaut trouvé en chemin, et qui justifie le point 4

`ultramodern` dérive `osGetCount` de `std::chrono::high_resolution_clock`. Mesuré
sur la cible : `is_steady=false`, et c'est un alias de `system_clock`. **Toute la
mesure du temps de DKR repose donc sur l'horloge murale**, qui recule quand
l'utilisateur change l'heure ou quand Windows applique l'heure d'hiver.

Brancher `osGetCount` sur cette base est donc justifié par une mesure, et non par
un souci de propreté. Ce branchement n'est **pas** encore fait : c'est un
changement de comportement de l'ordonnanceur qu'on ne peut pas exercer tant que
le jeu ne se lie pas (E01-S05, E02-S05, E07-S03).

## Risques

Une base de temps qui dérive lentement ne casse rien de visible et fausse tous les
chronométrages de course. Le contrôle de l'étape 4 doit donc porter sur une durée
longue — plusieurs minutes — et non sur un instantané.

## Références

- E02-S01 — couche d'attente
- E01-S03 — contournement de `GetTickCount64`
- `runtime-recomp/src/game/vi_presentation_policy.hpp` — politique de présentation
  actuelle, consommatrice de cette base
