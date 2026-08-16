# E04-S06 — Traduction de l'état RDP

| | |
|---|---|
| **Épic** | E04 — HLE F3DDKR indépendant de RT64 |
| **Statut** | IN_PROGRESS |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E04-S02 |
| **Bloque** | E05-S03, E05-S05, E05-S06 |

## Contexte

Le RDP de la N64 est piloté par un état dense : mode de cycle, combineur de
couleurs, mode de rendu, mélange, test de profondeur, brouillard, modes de
texture. Cet état est encodé dans quelques mots de 64 bits aux champs
entrelacés, et c'est lui qui détermine ce qui apparaît à l'écran.

Le combineur de couleurs mérite une mention à part. C'est une unité programmable
qui calcule, pour chaque pixel, une combinaison de texel, couleur de primitive,
couleur d'environnement, couleur de shading et constantes — sur un ou deux
cycles. Le combineur fixe de Glide est bien moins expressif, et la traduction est
le point dur de tout l'épic E05.

Le travail préparatoire existe déjà, et il vient du portage natif voisin : son
ticket E00-S01 a inventorié **33 configurations de combiner** effectivement
utilisées par DKR, dont **3 seulement lisent deux texels**
(`../../Diddy-Kong-Racing/docs/research/combiner-inventory.md`). Ce chiffre change
la nature du problème : il ne s'agit pas de traduire un combineur programmable en
général, mais 33 cas concrets et énumérés.

## Objectif

Traduire l'état RDP du jeu vers l'état de rendu abstrait défini en E04-S01, de
façon exhaustive et vérifiable.

## Périmètre

**Dans :** le décodage de l'état RDP et sa traduction vers l'état abstrait.

**Hors :** la réalisation de cet état par Glide (E05-S03 à E05-S06).

## Travail

1. Reprendre l'inventaire des 33 configurations du portage natif et **le
   revérifier** sur ce portage : même jeu, même version, mais un décodeur
   différent peut voir des configurations que l'autre normalise. L'inventaire est
   un point de départ solide, pas une vérité importée.
2. Décoder le mode de cycle. Le cycle unique et le double cycle ne se traduisent
   pas de la même façon : le double cycle correspond à deux étages de combinaison,
   donc potentiellement à deux passes ou à deux TMU côté Glide.
3. Décoder le combineur : les seize entrées possibles de chaque terme, sur un ou
   deux cycles, et les représenter sous une forme canonique et comparable. Cette
   forme canonique est ce qui permettra à E05-S03 de faire correspondre une
   configuration à un réglage Glide par simple recherche.
4. Décoder le mode de rendu : mélange, test alpha, tramage, test et écriture de
   profondeur, brouillard, anticrénelage.
5. Décoder les modes de texture : filtrage, enveloppement, miroir, niveaux de
   détail, conversion.
6. Instrumenter le décodeur pour qu'il journalise toute configuration rencontrée
   et non répertoriée. C'est le filet de sécurité : l'inventaire statique ne peut
   pas garantir d'avoir vu tous les chemins du jeu, et un cas manquant doit se
   signaler plutôt que produire un rendu faux en silence.
7. Rejouer une partie complète — tous les niveaux, tous les modes de jeu, les
   menus, les cinématiques — avec cette instrumentation, et compléter l'inventaire
   de ce qui remonte.
8. Écrire `docs/research/rdp-state-inventory.md` : la liste exhaustive des états
   rencontrés, leur fréquence, et la surface d'écran qu'ils couvrent. La fréquence
   et la surface décident de l'ordre de traitement en E05-S03.

## Critères d'acceptation

- [ ] L'inventaire de 33 configurations est revérifié sur ce portage, écarts
      consignés — **impossible par la même méthode**, et c'est la conclusion.
      Le voisin l'a dérivé des sources C de la décomposition ; ce portage-ci
      n'en dispose pas, il travaille depuis du MIPS recompilé. Son équivalent
      est l'instrumentation à l'exécution, qui demande la ROM.
- [x] Cycle unique et double cycle sont tous deux décodés et distingués — et le
      mode de cycle **fait partie de la clé canonique**, le même mot ne produisant
      pas la même image selon le cycle.
- [x] Le combineur est représenté sous une forme canonique comparable. Vérifié
      contre les **63 macros `G_CC_*` des en-têtes de la décomposition**,
      résolues et encodées par un générateur plutôt que transcrites : les 63 se
      décodent champ pour champ, sans collision de clé.
- [x] Les modes de rendu et de texture sont décodés — cycle, filtrage, LOD,
      détail, perspective, comparaison alpha, source de Z, test et écriture de
      profondeur. Le blender reste brut : il relève de E05-S05.
- [~] Toute configuration non répertoriée est journalisée à l'exécution. Le
      mécanisme existe — `dkr_rdp_combiner_name` rend `NULL` pour l'inconnu —
      mais **la table n'est amorcée qu'à huit entrées**, et rien ne journalise
      encore faute de décodeur en fonctionnement (E04-S02).
- [ ] Une partie complète est rejouée sous instrumentation — **bloqué** par
      E02-S06, qui demande la ROM.
- [~] `docs/research/rdp-state-inventory.md` existe et consigne ce qui est
      établi. **Fréquence et surface d'écran manquent** : les deux se mesurent à
      l'exécution, et le nombre d'entrées de table du voisin en est un substitut
      grossier — il compte des déclarations, pas des pixels.

> **Correction du 15 août 2026** : ce critère avait été marqué bloqué par l'absence de ROM. La ROM était présente — voir `docs/research/win95-rom-available.md`. Le blocage n'existe plus ; ce qui reste à faire l'est pour d'autres raisons, ou n'a simplement pas encore été fait.

## Risques

Une configuration manquée ne se voit pas au décodage : elle se voit à l'écran,
sous forme d'une surface d'une couleur inattendue, éventuellement dans un seul
niveau. L'instrumentation de l'étape 6 et la partie complète de l'étape 7 sont ce
qui distingue un inventaire réel d'un inventaire plausible.

## Références

- `../../Diddy-Kong-Racing/docs/research/combiner-inventory.md` — 33 configurations,
  3 à deux texels
- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — gestionnaires `MoveWord`,
  `SetTextureImage`, `LoadBlock`
- E05-S03 — consommateur principal de cet inventaire
