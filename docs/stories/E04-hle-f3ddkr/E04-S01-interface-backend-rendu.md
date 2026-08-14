# E04-S01 — Interface de backend de rendu

| | |
|---|---|
| **Épic** | E04 — HLE F3DDKR indépendant de RT64 |
| **Statut** | REVIEW |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E01-S02, E00-S05 |
| **Bloque** | E04-S02, E04-S08, E05-S01 |

## Contexte

Le projet dispose déjà d'une abstraction de rendu de haut niveau :
`ultramodern::renderer::RendererContext`, avec `send_dl`, `update_screen`,
`update_config` et `valid`. Deux implémentations existent — `RT64Renderer` et
`DiagnosticRenderer` — et une troisième, Glide, viendra s'y ajouter.

Mais cette interface est trop haute pour ce dont nous avons besoin. Elle reçoit
une tâche RSP brute et laisse l'implémentation faire tout le travail. Aujourd'hui,
`F3DDKRRT64Bridge` décode la display list **directement dans les structures de
RT64** : il appelle `RT64::State`, manipule des `RT64::DisplayList`, enregistre
des identités de charge de travail RT64. Le décodeur et le moteur de rendu sont
soudés.

Il faut les séparer, et donc introduire une seconde interface, plus basse : un
backend qui reçoit des primitives déjà transformées et un état de rendu abstrait.
Le décodeur F3DDKR devient alors indépendant du backend, et deux implémentations
peuvent le consommer : Glide, et un rastériseur logiciel de référence (E04-S08)
qui servira d'oracle de comparaison.

## Objectif

Définir `platform/render/backend.h` : l'interface que le décodeur F3DDKR pilote,
et que Glide comme le rastériseur logiciel implémentent.

## Périmètre

**Dans :** la définition de l'interface, ses structures de données, sa
documentation.

**Hors :** toute implémentation. C'est un ticket de conception, et son livrable
est un contrat.

## Travail

1. Lire `f3ddkr_rt64.cpp` (39 Ko) et relever exactement ce que le décodeur demande
   au moteur de rendu — pas ce qu'un moteur de rendu offre en général. La liste
   des primitives à produire se lit dans les gestionnaires déclarés par
   `f3ddkr_rt64.hpp` : matrices, sommets, triangles, rectangles pleins, image de
   texture, chargement de bloc, décalage de texture, mots d'état.
2. Concevoir l'interface autour de ce que Glide sait faire, puisque c'est la
   contrainte la plus dure. En particulier, le backend reçoit des sommets **déjà
   projetés en coordonnées écran** : aucune carte 3dfx ne transforme. La
   transformation, l'éclairage et le découpage restent côté décodeur (E04-S03,
   E04-S05).
3. Définir le vertex : position écran, profondeur, couleur, coordonnées de texture
   par unité de texture. Se caler sur la structure attendue par Glide pour éviter
   une recopie par sommet — sur un Pentium II, une conversion de format par sommet
   est un coût réel.
4. Définir l'état de rendu comme un bloc de valeurs, pas comme une série
   d'appels : combineur, mode de mélange, test de profondeur, test alpha,
   brouillard, texture liée, filtrage, enveloppement. Un bloc permet au backend de
   comparer à l'état courant et de n'émettre que les changements — c'est ce qui
   rend le suivi d'état bon marché.
5. Définir la gestion des textures comme un cache à handles : le décodeur fournit
   une texture décodée et une clé, le backend renvoie un handle et gère seul son
   placement en mémoire de texture (E05-S02).
6. Définir le cycle d'une image : début, séquences de dessin, fin, présentation.
7. Écrire la documentation de l'interface, en indiquant pour chaque élément ce que
   Glide sait faire nativement et ce qui devra être émulé. C'est ce document qui
   évitera de concevoir une interface que Glide ne peut pas honorer.

## Critères d'acceptation

- [x] `platform/render/backend.h` définit l'interface complète.
- [x] Chaque élément est justifié par un besoin réel relevé dans `f3ddkr_rt64.cpp`,
      pas par généralité. Le relevé a produit une contrainte qui décide de la
      forme : **le sommet DKR ne porte pas de coordonnées de texture** — ses dix
      octets sont `x, y, z` en 16 bits signés et `r, g, b, a` en octets — et les
      `s, t` arrivent **par coin, au moment du triangle**. Une interface à
      sommets indexés serait donc fausse ici ; l'expansion se fait côté décodeur.
- [x] La structure de vertex évite une conversion par sommet vers Glide — et ce
      n'est pas seulement documenté : `backend_layout_check.c` vérifie **à la
      compilation** que chaque champ est au décalage de `GrVertex`.
- [x] L'état de rendu est un bloc comparable, permettant l'émission
      différentielle. Vérifié aussi : le contrôle refuse tout remplissage, qui
      ferait comparer à `memcmp` des octets indéterminés.
- [x] L'interface est manifestement implémentable par Glide : chaque élément est
      annoté « NATIF » ou « A EMULER », avec l'appel Glide correspondant. Un seul
      relève de la seconde catégorie — le rectangle plein, que Glide ne connaît
      pas et que le backend fabrique en deux triangles — plus le combineur, dont
      la traduction est le travail de E05-S03.
- [x] Une implémentation vide compile et se lie — `backend_null.c`, qui compte ce
      qu'elle reçoit : un décodeur qui n'émet rien et un backend qui ne dessine
      rien se ressemblent beaucoup vus de l'écran.
- [x] L'interface n'expose aucun type propre à RT64, SDL2 ou ImGui.

## Risques

Une interface trop générique se paie deux fois : à l'écriture du backend Glide,
qui doit émuler ce que la carte ne fait pas, et à l'exécution, en surcoût par
primitive. Ici, l'interface doit épouser la carte plutôt que l'abstraire.

Une interface trop étroite, elle, empêchera le rastériseur logiciel de référence
de servir d'oracle. L'équilibre se trouve en écrivant les deux implémentations en
tête, pas une seule.

## Références

- `runtime-recomp/src/game/f3ddkr_rt64.hpp` — les quatorze gestionnaires de
  commandes du microcode
- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — décodeur actuel, soudé à RT64
- `ultramodern/renderer_context.hpp` — l'interface haute, conservée
- `docs/F3DDKR.md`
