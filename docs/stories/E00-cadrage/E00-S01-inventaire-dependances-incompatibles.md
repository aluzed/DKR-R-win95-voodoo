# E00-S01 — Inventaire des dépendances incompatibles avec Windows 95

| | |
|---|---|
| **Épic** | E00 — Cadrage, mesures et décisions |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | — |
| **Bloque** | E00-S02, E01-S02, E02-S01, E07-S03 |

## Contexte

DKR-R est un portage par recompilation statique conçu pour des systèmes 64 bits
modernes. La pile actuelle est, de haut en bas :

| Couche | Composant | Nature |
|---|---|---|
| Fenêtre / entrées / audio | SDL2 | dépendance externe |
| Interface | Dear ImGui (`runtime_ui.cpp`, 194 Ko) | dépendance externe |
| Rendu | RT64 (`extern/rt64`) | D3D12 / Vulkan / Metal |
| Ordonnancement N64 | `ultramodern` | C++20 |
| Chargement / API N64 | `librecomp` | C++20 |
| CPU du jeu | sortie N64Recomp (`RecompiledFuncs`) | C généré |
| Microcode audio | sortie RSPRecomp (`RecompiledRSP/aspMain.cpp`) | C++ généré |

Aucune de ces couches n'a été pensée pour un Win32 de 1995. Avant de planifier
quoi que ce soit, il faut savoir **précisément** ce qui tombe et pourquoi :
c'est ce qui distingue un remplacement obligatoire d'un simple ajustement.

## Objectif

Produire `docs/research/win95-blockers.md` : la liste exhaustive, composant par
composant, de ce qui empêche la compilation ou l'exécution sous Windows 95, avec
pour chaque entrée un verdict — **remplacer**, **patcher**, ou **conserver**.

## Périmètre

**Dans :** analyse statique des sources et des en-têtes des quatre dépendances
épinglées dans `dependencies.lock.json`, plus `runtime-recomp/src/game/`.

**Hors :** toute mesure de performance (c'est E00-S03 et E00-S04) et toute
écriture de code de remplacement.

## Travail

1. Préparer les dépendances une fois (`Prepare-DKR-Runtime.cmd` ou
   `scripts/bootstrap_dependencies.py`) pour disposer des worktrees à analyser.
2. **Appels d'API Win32.** Extraire tous les symboles importés depuis
   `kernel32`/`user32`/`advapi32` par `ultramodern`, `librecomp` et
   `runtime-recomp/src/game/`. Confronter chacun à la table d'exports réelle de
   Windows 95. Les manques attendus, à confirmer plutôt qu'à supposer :
   - `TryEnterCriticalSection` — NT 4 / 98 et au-delà ;
   - `InitializeCriticalSectionAndSpinCount` — 98 / NT 4 SP3 ;
   - `SignalObjectAndWait` — NT 4 ;
   - les variables de condition `SRWLOCK` / `CONDITION_VARIABLE` — Vista ;
   - `GetTickCount64`, `GetModuleHandleEx` — Vista / XP ;
   - toute la famille `...W` Unicode, qui est un stub sous 9x.
3. **Bibliothèque standard C++.** Relever les constructions C++20 qui excluent
   les compilateurs capables de cibler Win95 : concepts, `<ranges>`,
   `<span>`, `<bit>`, `consteval`, `<format>`, initialiseurs désignés, et
   surtout `std::thread` / `std::condition_variable` / `std::atomic` avec
   `std::latch` ou `std::jthread`. Compter les occurrences par fichier — c'est ce
   compte qui dira si `ultramodern` se patche ou se réécrit.
4. **Jeu d'instructions.** Repérer tout usage de SSE/SSE2/AVX, explicite
   (intrinsèques) ou implicite. Regarder en particulier
   `librecomp/include/librecomp/rsp_vu_impl.hpp`, inclus par
   `runtime-recomp/RecompiledRSP/aspMain.cpp:2` : l'unité vectorielle du RSP y est
   très probablement émulée en SSE2, absent du Pentium II.
5. **Hypothèses 64 bits.** Chercher les `static_assert(sizeof(void*) == 8)`, les
   conversions pointeur↔`uint64_t`, les espaces d'adressage réservés en dur
   (`librecomp` réserve typiquement la RDRAM par un `mmap`/`VirtualAlloc` massif).
6. **Dépendances externes.** Pour SDL2, ImGui et RT64, statuer sur le
   remplacement plutôt que le portage, et le justifier en une ligne chacun.
7. Ranger chaque conclusion dans le tableau de verdicts avec le ticket qui la
   traitera.

## Critères d'acceptation

- [ ] `docs/research/win95-blockers.md` existe et couvre les sept points ci-dessus.
- [ ] Chaque entrée porte un verdict **remplacer / patcher / conserver**, une
      justification d'une ligne, et le ticket qui s'en charge.
- [ ] Les API Win32 manquantes sont vérifiées contre une source d'exports réelle
      (table d'exports d'un `kernel32.dll` de Win95 OSR2.5, ou la colonne
      « Minimum supported client » de la documentation Win32), pas de mémoire.
- [ ] Le compte des constructions C++20 est donné **par fichier** pour
      `ultramodern` et `librecomp`, de sorte que E01-S02 puisse trancher entre
      patch et réécriture sur un chiffre.
- [ ] Le document nomme explicitement les composants qui survivent sans
      modification — c'est là que se trouve la valeur conservée du projet.

## Risques

Le piège est de conclure « tout est à jeter ». Le cœur du projet — la sortie
N64Recomp, qui est du C généré à partir d'un ELF MIPS — n'a aucune raison
d'être incompatible : c'est du C portable manipulant des entiers. Si l'inventaire
conclut à un remplacement total, il est faux.

## Références

- `dependencies.lock.json` — les quatre dépendances épinglées
- `docs/ARCHITECTURE.md` — frontières protégées et couches
- `runtime-recomp/CMakeLists.txt:26-31` — normes C17 / C++20 exigées
- `runtime-recomp/RecompiledRSP/aspMain.cpp:1-2` — inclusions `librecomp`
