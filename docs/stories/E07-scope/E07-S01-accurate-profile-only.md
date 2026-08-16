# E07-S01 — Profil « Accurate » seul

| | |
|---|---|
| **Épic** | E07 — Réduction de périmètre |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E00-S07 |
| **Bloque** | E06-S04, E06-S05, E08-S04 |

## Contexte

DKR-R propose deux profils. **Accurate** reproduit la console : 4:3, 30 images par
seconde, champ de vision, distance d'affichage, niveau de détail et mixage audio
d'origine. **Modern** ajoute le format large, l'interpolation vers les taux de
rafraîchissement élevés, un champ de vision élargi, une distance de décor étendue
et le filtrage anisotrope.

Sur une machine qui aura du mal à tenir 30 images par seconde en 640 × 480, le
mode Moderne n'a aucun sens. Il coûte, en revanche, beaucoup de complexité :
`presentation_identity.cpp` fait 39 Ko et n'existe que pour attribuer des
identités sémantiques stables aux objets en mouvement afin que RT64 puisse
interpoler entre deux images. Toute cette machinerie — sidecar d'identités,
crochets à la naissance et à la mort des objets, `TaskIdentityScope`, appariement
des charges de travail — disparaît avec le mode Moderne.

C'est la plus grosse simplification disponible dans ce projet, et elle allège à la
fois le code, la mémoire et le CPU.

## Objectif

Ne conserver que le profil Accurate, et retirer la machinerie qui n'existait que
pour le mode Moderne.

## Périmètre

**Dans :** la suppression du mode Moderne et de ses dépendances.

**Hors :** ImGui, les texture packs et la télémétrie (E07-S02).

## Travail

1. Établir la liste exacte de ce qui disparaît, à partir de `presentation_policy.hpp`
   et de la frontière décrite dans `docs/ARCHITECTURE.md` : format large,
   interpolation, champ de vision, distance de décor, niveau de détail des
   véhicules, filtrage anisotrope, caméra moderne, gyroscope.
2. Retirer les identités de présentation. `presentation_identity.cpp` (39 Ko) et
   `presentation_identity.hpp` (17 Ko) sortent, ainsi que les crochets du pipeline
   de patchs qui les alimentent — chargement de scène, naissance et libération
   d'objet, frontière `render_object`. Ces crochets sont dans la politique de
   recompilation, pas dans le code du projet : les retirer allège aussi le code
   généré.
3. Retirer l'interpolation : `interpolation_state_policy.hpp`, et les patchs RT64
   qui la servent (`0001-allow-skip-buffering-interpolation-targets`,
   `0002-count-interpolated-presentations`). Ces patchs ne concernent que la cible
   moderne — vérifier avant de les retirer si l'oracle en dépend (E00-S07).
4. Retirer `widescreen_policy.hpp`, `modern_camera_policy.hpp`,
   `motion_steering_policy.hpp`.
5. Simplifier `renderer_snapshot` et la profondeur de file de tâches graphiques,
   conformément à la décision de E00-S06 : sans interpolation, il n'y a plus besoin
   d'apparier deux images.
6. Trier les suites de tests correspondantes : `interpolation_state_policy_tests`,
   `presentation_identity_tests`, `widescreen_policy_tests`,
   `modern_camera_policy_tests`, `motion_steering_policy_tests`. Elles sortent avec
   le code qu'elles couvrent.
7. Mesurer le gain : lignes de code, taille du binaire, mémoire, et temps CPU par
   image. Le dernier chiffre est le plus intéressant — les crochets d'identité
   s'exécutaient à chaque objet rendu.
8. Vérifier que le comportement du profil Accurate est strictement inchangé. Il
   est la référence de régression du projet (`docs/ARCHITECTURE.md`) et rien ne
   doit bouger.

## Critères d'acceptation

- [ ] Le mode Moderne et toutes ses dépendances sont retirés.
- [ ] Les identités de présentation et leurs crochets de recompilation sont
      retirés.
- [ ] Les patchs RT64 propres à l'interpolation sont retirés, ou conservés avec
      justification si l'oracle en dépend.
- [ ] Les suites de tests devenues sans objet sont retirées.
- [ ] Le gain est mesuré : code, binaire, mémoire, temps CPU par image.
- [ ] Le comportement du profil Accurate est inchangé, vérifié par comparaison
      avant / après.
- [ ] La documentation ne mentionne plus le mode Moderne.

## Risques

Certaines de ces politiques peuvent être plus enchevêtrées dans le code du jeu
qu'il n'y paraît : les crochets d'identité sont dans la politique de
recompilation, et les retirer change le code généré. Vérifier après régénération
que le jeu se comporte identiquement, plutôt que de supposer qu'un retrait est
neutre.

## Références

- `docs/ARCHITECTURE.md` — frontière entre profils, Accurate comme référence de
  régression
- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md` — machinerie d'identités sémantiques
- `runtime-recomp/src/game/presentation_identity.{hpp,cpp}`,
  `interpolation_state_policy.hpp`, `presentation_policy.hpp`
- `runtime-recomp/dkr.us.v77.recomp-policy.json` — crochets à retirer
