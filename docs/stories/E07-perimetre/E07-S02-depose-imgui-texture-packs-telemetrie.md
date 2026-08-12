# E07-S02 — Dépose d'ImGui, des texture packs et de la télémétrie

| | |
|---|---|
| **Épic** | E07 — Réduction de périmètre |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | ~~M~~ **S** |
| **Dépend de** | E06-S05 |
| **Bloque** | E07-S03 |

## État au 2026-08-12 — l'essentiel est déjà fait par un interrupteur existant

[E00-S01](../E00-cadrage/E00-S01-inventaire-dependances-incompatibles.md) a
constaté que `runtime-recomp/CMakeLists.txt` place déjà ces composants derrière
`DKR_RUNTIME_BUILD_RT64`, dont la valeur par défaut est **`OFF`** :
`runtime_ui.cpp` (ImGui), `runtime_texture_packs.cpp`,
`runtime_rice_texture_import.cpp`, `runtime_crt_overlay.cpp`, `f3ddkr_rt64.cpp`,
`rt64_renderer.cpp` et le pont ImGui/SDL.

Conséquences chiffrées :

- **1 054 des 1 089 références ImGui** (97 %) sont dans `runtime_ui.cpp`, déjà exclu ;
- **139 des 299 usages de `std::filesystem`** (46 %) disparaissent avec eux.

Il n'y a donc **rien à supprimer** — ce qui préserve la cible moderne, qui
continue d'allumer l'interrupteur.

Reste le seul vrai travail : `runtime_platform.cpp` contient **31 références
ImGui non gardées** (lignes 418-458), qui recopient l'état de la manette dans
`ImGuiIO`. Ce fichier fait partie du socle et doit être découplé.

La télémétrie (`runtime_telemetry.cpp`) n'est, elle, pas sous garde : à traiter.

## Contexte

Plusieurs sous-systèmes de DKR-R n'ont pas de place sur cette cible, et ils
représentent une part considérable du code du projet :

| Composant | Taille | Motif |
|---|---|---|
| `runtime_ui.cpp` | 194 Ko | overlay ImGui, remplacé par le fichier `.ini` (E06-S05) |
| `runtime_texture_packs.cpp` | 28 Ko | packs RT64 / Rice — la mémoire de texture ne le permet pas |
| `runtime_rice_texture_import.cpp` | 9 Ko | idem |
| `runtime_crt_overlay.cpp` | 10 Ko | filtres CRT en ImGui — sans objet sur un vrai moniteur cathodique |
| `runtime_telemetry.cpp` | 4 Ko | à réévaluer, un affichage de compteurs reste utile (E08-S01) |
| `save_manager.cpp` | 25 Ko | interface graphique ImGui ; le codec sous-jacent est conservé (E02-S05) |
| `runtime_magic_codes.cpp` | 7 Ko | à conserver s'il ne dépend pas d'ImGui |

Le cas des texture packs mérite d'être explicite : ce n'est pas un renoncement
esthétique mais une contrainte matérielle. Une Voodoo 2 dispose de 2 à 4 Mo de
mémoire de texture par TMU, et le jeu d'origine en occupe déjà 1,20 Mo au pic
(E05-S02). Des textures haute résolution n'y tiennent pas, quelle que soit
l'envie qu'on en ait.

Le filtre CRT, lui, a une ironie propre : la machine cible est très probablement
reliée à un véritable moniteur cathodique.

## Objectif

Retirer les sous-systèmes sans objet sur cette cible, en préservant ce qui garde
une utilité.

## Périmètre

**Dans :** le retrait de ces composants et de leurs dépendances.

**Hors :** le mode Moderne (E07-S01) et le découplage de SDL2 (E07-S03).

## Travail

1. Retirer `runtime_ui.cpp` et toutes les dépendances à ImGui. C'est le plus gros
   retrait du projet, et il élimine une dépendance externe entière.
2. Retirer les texture packs et l'import Rice, ainsi que le patch RT64 associé
   (`0011-enable-runtime-rice-texture-aliases`,
   `0012-cache-rice-replacement-decisions`) si l'oracle n'en dépend pas.
3. Retirer l'overlay CRT.
4. Retirer l'interface graphique de gestion des sauvegardes, en **conservant**
   `dkr_save_codec.cpp` et la logique de `virtual_pak.cpp` (E02-S05). La
   séparation entre les deux est le point de vigilance de ce ticket.
5. Réévaluer la télémétrie : un affichage de compteurs à l'écran reste précieux
   pour E08-S01. Conserver la collecte, remplacer l'affichage ImGui par un rendu
   minimal via les rectangles 2D de E05-S07.
6. Vérifier les magic codes : les conserver s'ils ne dépendent que de la
   configuration, les retirer s'ils exigent une interface.
7. Trier les suites de tests correspondantes : `rice_texture_pack_policy_tests`
   sort, `save_manager_tests` est à revoir selon ce qui est conservé,
   `magic_code_policy_tests` suit la décision de l'étape 6.
8. Mesurer le gain en taille de binaire et en mémoire.

## Critères d'acceptation

- [ ] Aucune dépendance à ImGui ne subsiste dans la cible Win95.
- [ ] Texture packs, import Rice et overlay CRT sont retirés, patchs associés
      compris.
- [ ] Le codec de sauvegarde et la logique de Controller Pak sont conservés et
      fonctionnels.
- [ ] La collecte de télémétrie est conservée, son affichage remplacé.
- [ ] La décision sur les magic codes est prise et appliquée.
- [ ] Les suites de tests sont triées en conséquence.
- [ ] Le gain en binaire et en mémoire est mesuré.
- [ ] Aucune fonctionnalité de jeu n'est perdue au passage — seulement des
      fonctionnalités de portage.

## Risques

`save_manager.cpp` mêle interface et logique. Retirer l'interface sans emporter la
logique demande de la précision : une erreur ici casse les sauvegardes, ce qui est
le défaut le moins pardonnable du projet (E02-S05). Le tester avant et après.

## Références

- `runtime-recomp/src/game/runtime_ui.cpp`, `runtime_texture_packs.cpp`,
  `runtime_crt_overlay.cpp`, `runtime_telemetry.cpp`, `save_manager.cpp`
- `docs/TEXTURE_PACKS.md`
- E05-S02 — budget de mémoire de texture qui exclut les packs
