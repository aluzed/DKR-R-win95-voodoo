# ADR 0002 — Cible matérielle et version de Glide

- **Statut** : accepté
- **Date** : 2026-08-12
- **Ticket** : [E00-S05](../stories/E00-cadrage/E00-S05-adr-cible-materielle-glide.md)

## Contexte

« Compatible 3dfx Voodoo » désigne cinq générations de cartes et deux API. Le
choix décide directement du travail de rendu — en particulier le nombre de TMU,
qui détermine si les combiners à deux texels passent en une passe ou en deux.

Contrainte commune à toute la gamme : **aucune transformation matérielle**. La
carte reçoit des sommets déjà projetés. Tout le pipeline géométrique reste à la
charge du CPU, qui est déjà le poste tendu ([E00-S03](../research/cpu-budget.md)).

## Décision

| Élément | Plancher | Recommandé |
|---|---|---|
| CPU | Pentium II 400 MHz | Pentium III 500 MHz et plus |
| RAM | **64 Mo** | 128 Mo |
| Carte 3dfx | Voodoo 2, 8 Mo, **2 TMU** | Voodoo 2 12 Mo ou Voodoo 3 |
| API | **Glide 2.4x** (`glide2x.dll` 2.54) | idem |
| Système | Windows 95 OSR2 ou OSR2.5 | idem |
| Résolution | **640 × 480, 16 bits, double tampon + Z** | idem |

Chaque valeur est justifiée ci-dessous par une mesure ou une contrainte
matérielle. Aucune ne relève d'une préférence.

### RAM : 64 Mo, et le chiffre est mesuré

[L'ADR 0003](0003-budget-memoire.md) a relevé sur la machine : Windows 95 et ses
pilotes consomment **15,6 Mio**, la pile 3dfx **872 Kio**, et il reste **47,0 Mio**
disponibles pour un budget projet de 32,8 Mio. Sur 32 Mo il ne resterait que
~16 Mio : la cible n'est pas tenable et n'est pas retenue.

### Résolution : 640 × 480 en 16 bits, double tampon

Le calcul est contraint par la mémoire d'image de la carte, qui est séparée de
la mémoire de texture :

| Configuration | Besoin | Voodoo 2 8 Mo (2 Mo image) | Voodoo 2 12 Mo (4 Mo image) |
|---|---:|---|---|
| Double tampon + Z | 1,76 Mio | ✅ 88 % occupé | ✅ 44 % |
| **Triple** tampon + Z | 2,34 Mio | ❌ **ne tient pas** | ✅ 59 % |

640 × 480 × 2 octets = 614 400 octets par tampon ; trois tampons (image, image
arrière, profondeur) font 1,76 Mio.

**Le triple buffering est donc écarté**, parce qu'il exclurait la Voodoo 2 8 Mo
qui est le plancher. Ce n'est pas une perte : le triple buffering sert à lisser
une cadence irrégulière, or E00-S03 annonce une machine à la peine — la latence
qu'il ajoute serait payée sans le bénéfice.

### TMU : deux exigées, deux exploitées, une passe de repli

**Deux TMU sont exigées au plancher.** Une seule TMU imposerait une seconde passe
sur toutes les surfaces à deux texels, donc un budget de remplissage doublé —
sur une machine dont le CPU est déjà le facteur limitant.

Le rendu doit néanmoins **rester correct sur une seule TMU**, par repli
multipasse ([E05-S04](../stories/E05-glide/E05-S04-multitexture-deux-tmu.md)) :
correct, pas rapide. C'est ce qui permet à une Voodoo 1 d'afficher le jeu sans
que le projet ait à la soutenir.

### Mémoire de texture : le jeu tient en résidence totale

Le portage natif voisin a résolu chaque niveau jusqu'à ses textures réelles
(`../../Diddy-Kong-Racing/docs/research/level-working-set.md`), sur **65 niveaux
dont 62 circuits et hubs** — soit le même jeu et le même jeu de niveaux que ce
portage :

| Grandeur | Mesure |
|---|---:|
| Pic total (pire circuit + permanent + grille) | **1 116 Ko** |
| Pic avec padding en puissances de deux | **1 225 Ko** |

Confronté à la mémoire par TMU :

| TMU | Pic padé | Occupation |
|---|---:|---:|
| 2 Mo (Voodoo 2 8 Mo) | 1,20 Mio | **60 %** |
| 4 Mo (Voodoo 2 12 Mo) | 1,20 Mio | 30 % |

**Le pire niveau tient intégralement dans une TMU de 2 Mo**, avec 40 % de marge.
La gestion de la mémoire de texture (E05-S02) peut donc viser la résidence
totale par niveau plutôt qu'un cache avec éviction en cours de course — ce qui
supprime un poste de complexité et une source de à-coups.

### API : Glide 2.4x

Le ticket demandait d'argumenter sur la couverture réelle des sources 3dfx
ouvertes, et non sur la documentation commerciale d'époque. Le `README` du dépôt
[sezero/glide](https://github.com/sezero/glide) donne les noms internes 3dfx :

```
sst1:  Voodoo Graphics
sst96: Voodoo Rush
cvg:   Voodoo 2
h3:    Voodoo Banshee/Voodoo 3
```

et les arbres construisent :

| Arbre | Cibles présentes |
|---|---|
| `glide2x` | `sst1`, `cvg`, `h3` |
| `glide3x` | `sst1`, `cvg`, `h3`, `h5` |

**L'idée reçue selon laquelle Glide 2.4 serait la seule voie vers la Voodoo 1 est
donc fausse** : `glide3x` a une cible `sst1`. Inversement, `glide2x` couvre la
Voodoo 3. Les deux arbres couvrent l'intégralité de la cible retenue, et le
choix ne se joue pas sur le matériel.

Il se joue sur trois faits mesurés sur la machine :

1. **Glide 2.54 est prouvé de bout en bout.** [E09-S01](../stories/E09-qa/E09-S01-environnement-test-emule.md)
   affiche un triangle Gouraud ; `tools/win95/probes/` interroge le matériel et
   mesure la mémoire par la même DLL.
2. **Glide 3.x exige un HWND valide.** `tools/win95/probes/glide3_probe.c` charge
   `glide3x.dll`, trouve tous ses exports, mais `grSstWinOpen` refuse :
   « *need to use a valid window handle* ». Glide 2.x accepte `0` et prend
   l'écran en plein écran. Retenir Glide 3 coupleraît donc l'amorçage du rendu à
   [E06-S01](../stories/E06-plateforme/E06-S01-fenetre-win32-boucle-messages.md),
   alors qu'il peut aujourd'hui en être indépendant.
3. **Les deux DLL sont installées** par le pilote de référence 3dfx pour Voodoo 2
   (`GLIDE2X.DLL` 398 848 o, `GLIDE3X.DLL` 425 472 o, toutes deux du 11 octobre
   1998). Le choix n'a donc pas de coût de distribution.

**Ce que Glide 3 aurait apporté**, et qu'il faut consigner puisque c'est ce qui
rouvrirait la décision : `grVertexLayout`, qui permet de *déclarer* le format de
sommet au lieu de remplir une structure figée de 60 octets. C'est exactement le
piège qui a coûté du temps en E09-S01 — un sommet rouge sortant vert parce que
`ooz` et `a` s'intercalent entre les couleurs et `oow`. Sur une machine où le CPU
est le facteur limitant, réduire le travail par sommet n'est pas cosmétique.

**L'API est donc placée derrière l'interface de backend de
[E04-S01](../stories/E04-hle-f3ddkr/E04-S01-interface-backend-rendu.md)**,
de sorte qu'un backend Glide 3 puisse s'ajouter sans toucher au décodeur F3DDKR.

## Ce que la machine de test dit d'elle-même

`tools/win95/probes/glide_hwinfo.c` interroge `grSstQueryHardware`, par le même
chemin que le moteur utilisera (E05-S01).

### Correction du 14 août 2026 — la machine est bien une Voodoo 2

La version précédente de cette section concluait que le fichier de configuration
de 86Box « mentait », Glide rapportant le type `0` alors que le fichier annonçait
`type = 2`. **Cette conclusion était fausse, et la méthode l'était aussi.**

Il y a deux sections Voodoo dans `86box.cfg`. Celle que 86Box lit porte le
suffixe d'instance — `[3dfx Voodoo Graphics #1]` — et elle disait `type = 1`,
c'est-à-dire **Obsidian SB50 + Amethyst**, un Voodoo 1 à deux TMU. 86Box
l'honorait fidèlement. L'autre section, écrite à la main, annonçait `type = 2` et
n'était simplement jamais lue.

La machine est désormais configurée sur la carte **plancher** de cette ADR, et le
dialogue de réglages — la seule source qui fasse foi, comme E09-S01 l'avait déjà
établi — le confirme :

```text
Type de Voodoo                     : 3Dfx Voodoo 2
Taille memoire du tampon d'images  : 2 Mo
Taille memoire des textures        : 2 Mo
```

### Et Glide 2.54 rapporte quand même le type 0

Relevé de `grSstQueryHardware` sur cette machine, une fois la Voodoo 2 en place :

```text
version Glide : 2.54
cartes detectees : 1
carte 0
  type          : 0 (Voodoo Graphics)
  memoire image : 2 Mo
  revision FBI  : 261
  TMU           : 2
  TMU 0 memoire : 2 Mo
  TMU 1 memoire : 2 Mo
```

Le type reste `0`, et la révision FBI reste `261` — **exactement les mêmes valeurs
que sur l'Obsidian**. Seules les tailles mémoire ont changé.

**Conséquence pour E05-S01, et elle est concrète : sur cette plate-forme,
`grSstQueryHardware` ne permet pas de distinguer une Voodoo 1 d'une Voodoo 2.**
`GrSstType` de Glide 2.x ne sépare pas les deux — `GR_SSTTYPE_VOODOO` couvre la
famille, et `glide2x` 2.54 *est* le pilote Voodoo 2. La détection à l'exécution
doit donc reposer sur autre chose que le type : le nombre de TMU et la mémoire
par TMU sont exploitables ; le modèle exact ne l'est pas.

Réserve de portée : ce relevé est celui de l'**émulation**. Sur du matériel réel
la révision FBI diffère entre les deux générations, et pourrait discriminer.
C'est à vérifier en E09-S04, et c'est une raison de plus de ne pas faire reposer
la détection sur elle.

### Ce que cela change au poids de E09-S04

La version précédente concluait que « la machine de test n'exerce pas les chemins
spécifiques à la Voodoo 2 », ce qui augmentait le poids de la validation sur
matériel réel. **Ce n'est plus vrai** : la machine émule désormais une Voodoo 2,
et sur la configuration plancher — 2 Mo de tampon d'images et 2 Mo par TMU, les
deux contraintes les plus serrées de cette ADR. Le budget de texture de E05-S02
sera donc éprouvé contre la vraie limite, et non contre le double.

## Configuration de validation sur matériel réel

E09-S04 déclarera la version bonne sur :

| | Configuration de validation |
|---|---|
| **Plancher** | Pentium II 400 MHz, 64 Mo, **Voodoo 2 8 Mo** (2 TMU × 2 Mo), Windows 95 OSR2.5 |
| **Recommandée** | Pentium III 500 MHz, 128 Mo, **Voodoo 2 12 Mo** ou Voodoo 3 2000, Windows 95 OSR2.5 |

La configuration plancher est celle qui compte : c'est elle qui met à l'épreuve
les 2 Mo par TMU et les 2 Mo de mémoire d'image, donc les deux contraintes les
plus serrées de cette ADR.

## Ce qui rouvrirait cette décision

- **Un dépassement du budget de texture découvert en E05-S02.** Le pic de
  1,20 Mio vient d'une analyse statique du portage voisin ; si le décodage réel
  (E04-S07) produit davantage — mipmaps, formats non compressés — la résidence
  totale tombe et il faut un cache. Au-delà de 2 Mo par niveau, le plancher passe
  à la Voodoo 2 12 Mo.
- **Un coût mesurable du remplissage de `GrVertex`** relevé en
  [E08-S03](../stories/E08-perf/E08-S03-optimisation-chemin-vertex.md). C'est
  l'argument qui ferait basculer sur Glide 3 et son `grVertexLayout`.
- **Le go/no-go de E00-S03**, qui n'est pas encore tombé : le plancher CPU
  ci-dessus est **provisoire**. E00-S03 a mesuré un facteur combiné de ~38× entre
  l'hôte et la cible, mais le verdict attend une vraie session de jeu
  ([E02-S06](../stories/E02-systeme/E02-S06-amorcage-jeu.md)). Si ce verdict est
  négatif, cette ADR relève le plancher ou acte la sortie — elle ne contourne pas
  le chiffre.
- Une demande de support Voodoo 4/5, qui imposerait Glide 3 (`h5`).

## Références

- [`docs/adr/0003-budget-memoire.md`](0003-budget-memoire.md) — 47 Mio disponibles, coût de Glide
- [`docs/research/cpu-budget.md`](../research/cpu-budget.md) — facteur 38×, go/no-go en attente
- `../../Diddy-Kong-Racing/docs/research/level-working-set.md` — pic de 1 116 Ko sur 65 niveaux
- `tools/win95/probes/glide_hwinfo.c` — interrogation du matériel
- `tools/win95/probes/glide3_probe.c` — essai de Glide 3.x
- [sezero/glide](https://github.com/sezero/glide) — arbres `glide2x` et `glide3x`, cibles réelles
