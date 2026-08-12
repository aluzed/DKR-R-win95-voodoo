# E09-S01 — Environnement de test émulé

| | |
|---|---|
| **Épic** | E09 — Intégration, QA et distribution |
| **Statut** | REVIEW |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | — |
| **Bloque** | E00-S02, E05-S01, E09-S02 |

## État au 2026-08-11

**L'environnement est opérationnel de bout en bout** : Windows 95 OSR2.5 sur
Pentium II / Voodoo 2, pilote 3dfx installé, et une démonstration Glide qui rend
un triangle Gouraud à l'écran. La machine se pilote sans écran physique.
Recette : [`docs/TEST-ENVIRONMENT.md`](../../TEST-ENVIRONMENT.md).

| Étape | État |
|---|---|
| 86Box v6.0 installé et exécutable, sans droits root | ✅ |
| Machine POST : Pentium II 400 MHz, 65 536 Ko, 3 disques détectés | ✅ vérifié par capture d'écran |
| Images disque partitionnées et formatées depuis l'hôte | ✅ |
| Source Windows 95 extraite de l'ISO du joueur vers E: | ✅ 63 fichiers, 46 Mio |
| Amorçage FreeDOS, C:/D:/E: visibles par DOS | ✅ prouvé par témoin écrit depuis l'invité |
| Transfert hôte ↔ invité par `mtools`, sans root | ✅ dans les deux sens |
| Pilotage sans écran : capture + injection de touches | ✅ `Drive-Win95-VM.sh` |
| **Windows 95 OSR2.5 installé et démarre** | ✅ ScanDisk sans erreur sur C:, D:, E: |
| Sound Blaster 16 détectée par Windows | ✅ |
| Instantané / restauration | ✅ `Run-Win95-VM.sh --snapshot` / `--restore` |
| Configurations Voodoo 1 et machine lente | ✅ par variables d'environnement |
| **Pilote 3dfx 3.01.00 installé et lié à la carte** | ✅ « Voodoo2 3D Accelerator », sans avertissement |
| **Runtime Glide en place** | ✅ `glide2x.dll`, `glide3x.dll`, `fxmemmap.vxd` dans `C:\WINDOWS\SYSTEM` |
| **Démonstration Glide** | ✅ contexte 640×480, effacements, échanges, triangle Gouraud |

Journal écrit par la démonstration depuis l'invité :

```text
glide2x.dll charge / symboles Glide resolus / grGlideInit
cartes 3dfx detectees : 1
contexte 640x480 ouvert, double buffer
grBufferClear + grBufferSwap x3
grDrawTriangle : triangle Gouraud
SUCCES : la pile Glide fonctionne de bout en bout
```

Livré : `Setup-Win95-TestVM.sh`, `prepare_win95_install.py`, `patch_voodoo2_inf.py`,
`Run-Win95-VM.sh`, `Drive-Win95-VM.sh`, `Push-To-Win95-VM.sh`,
`tools/win95/azerty_keys.py`, `tools/win95/glidetest.c` + `build-glidetest.sh`,
`docs/TEST-ENVIRONMENT.md`.

### Ce que le montage a appris

**Le pilotage sans écran est acquis**, et il vaut bien au-delà de l'installation :
c'est le socle du harnais de comparaison visuelle de
[E09-S02](E09-S02-harnais-comparaison-visuelle.md), qui pourra donc tourner en
intégration continue. Deux pièges le conditionnent, tous deux documentés :
`xdotool key --window` passe par `XSendEvent` que Qt ignore, et 86Box ne route le
clavier vers l'invité qu'après un clic de capture dans sa fenêtre.

**Le diagnostic par capture d'écran a été décisif.** Quatre tentatives
d'amorçage ont échoué sans laisser de trace exploitable ; la capture a montré la
cause en une image — le BIOS attendait `Press F1 to continue` sur une CMOS
vierge. De l'extérieur, une machine bloquée au BIOS et une machine qui n'amorce
pas sont indiscernables.

**Quatre erreurs de montage trouvées et corrigées**, chacune invisible autrement :
un disque dur DOS exige une table de partition — un volume FAT brut n'est pas vu ;
la partition système doit porter le fanion actif, faute de quoi Windows
s'installerait sans pouvoir démarrer ; 86Box remet à `none` toute valeur de
configuration qu'il refuse, ce qui avait silencieusement supprimé la carte 2D
(`virge375_pci`, et non `s3_virge_375_pci`) — sans carte vidéo, la machine ne
POST pas ; et surtout la géométrie de disque, ci-dessous.

**Le piège de la géométrie, qui a coûté le plus cher.** Un BIOS d'époque
n'adresse que 1024 cylindres ; au-delà il double les têtes jusqu'à repasser sous
la limite et présente **cette** géométrie translatée à `INT 13h`. Le code
d'amorçage de Windows 95 convertit ses adresses en CHS avec le nombre de têtes
inscrit dans le BPB du secteur de démarrage. Écrit depuis Linux avec 16 têtes
alors que le BIOS en présentait 64, chaque lecture tombait à côté : la machine
chargeait n'importe quoi et se figeait **sans le moindre message**, exactement
comme un disque non amorçable.

Le POST le signalait pourtant, en une colonne : `Pri. Master : LBA` contre
`Sec. Slave : CHS`. Seul le disque système dépassait 1024 cylindres, et c'était
le seul à ne pas démarrer. `prepare_win95_install.py` calcule désormais la
géométrie translatée **avant** de créer les images.

Écartés en chemin, chacun testé : détection de virus au démarrage, séquence
d'amorçage, `Halt On: All Errors`, présence du lecteur de disquette, et
réécriture du MBR par `FDISK /MBR`.

**La machine a émulé une Voodoo 1 pendant toute l'installation.** 86Box n'a pas
appliqué les réglages Voodoo écrits à la main dans `86box.cfg` : le fichier
disait `type = 1` et 4 Mo, le dialogue affichait « Graphique 3dfx Voodoo » et
2 Mo. Il a conservé le texte tout en émulant autre chose. Symptômes : le POST
listait `121A 0001`, et Glide répondait « expected Voodoo, none detected ».
Corrigé en passant une fois par le dialogue de réglages ; les mêmes valeurs sont
ensuite honorées.

**Ce point remonte à [E00-S05](../E00-cadrage/E00-S05-adr-cible-materielle-glide.md) :**
le modèle de carte doit être vérifié dans le dialogue, jamais déduit du fichier.
Un budget de mémoire de texture mesuré sur 2 Mo au lieu de 4, ou un test de
multitexture mené sur une seule TMU, serait faux sans que rien ne le signale.

**Un piège de Glide trouvé par la démonstration, et qui vaut pour tout E05.** La
structure `GrVertex` de Glide 2.x range ses champs dans l'ordre
`x, y, z, r, g, b, ooz, a, oow` : `ooz` et `a` s'intercalent entre les couleurs
et `oow`. Une structure « logique » compile sans avertissement et rend un
triangle impeccable **aux couleurs permutées** — un sommet rouge sort vert. Ni le
compilateur ni Glide ne signalent quoi que ce soit ; seule la comparaison
visuelle l'attrape. C'est l'argument de
[E04-S08](../E04-hle-f3ddkr/E04-S08-rasteriseur-logiciel-reference.md) en
miniature.

**Le pilote 3dfx a demandé deux corrections.** L'assistant de mise à jour de
pilote n'expose pas « Disquette fournie » — il filtre les modèles par la classe
du périphérique existant, et un périphérique inconnu ne correspond à aucune
classe. C'est **Panneau de configuration → Ajout de périphérique** qui fait lire
l'INF. Et surtout : **86Box expose sa Voodoo 2 avec l'identifiant PCI de la
Voodoo 1** (`121A:0001`), alors que `voodoo2.inf` ne se lie qu'à `DEV_0002`. Le
pilote d'origine ne peut donc pas reconnaître la carte émulée.
`scripts/patch_voodoo2_inf.py` ajoute la liaison manquante.

Conséquence directe pour
[E05-S01](../E05-glide/E05-S01-initialisation-glide-buffers.md) : **la détection
de carte à l'exécution ne doit pas se fier à l'identifiant PCI**, qui ment sur
cette plate-forme de test. Une détection naïve croirait avoir affaire à une
Voodoo 1 à une seule TMU et prendrait le repli multipasse de E05-S04 sans raison.
`grSstQueryBoards` et `grGet` font foi.

**Une réponse pour le backlog graphique.** Le pilote 3dfx 3.01.00 embarque
**Glide 2.54 et Glide 3.01** pour Voodoo 2 : le choix de version d'API dans
[E00-S05](../E00-cadrage/E00-S05-adr-cible-materielle-glide.md) ne dépend donc
pas du matériel — les deux runtimes sont sur la machine.

Le modèle de carte, en revanche, ne se lit nulle part de façon fiable : le
gestionnaire de périphériques affiche « Version du matériel : 002 », qui est la
**révision** et non l'identifiant, et l'identifiant PCI dit `0001`, celui de la
Voodoo 1. Seul le pilote installé — « Voodoo2 3D Accelerator » — atteste du
modèle. Raison de plus pour que E05-S01 interroge Glide plutôt que le bus.

**Sur l'ISO fournie :** elle n'est pas amorçable (pas d'El Torito), et le CD
OSR2.5 français n'a pas de `SETUP.EXE` — son installateur s'appelle
`INSTALL.EXE`. D'où la disquette FreeDOS et la source posée sur un disque dur
plutôt que sur le CD, ce qui supprime toute dépendance à un pilote CD-ROM DOS.

86Box a placé une carte 2D S3 ViRGE aux côtés de la Voodoo 2 : ce n'est pas un
artifice d'émulation mais le montage réel d'une Voodoo 2, qui se branche en
sortie de la carte 2D et ne prend la main qu'en 3D plein écran — ce qui confirme
l'hypothèse d'intégration de [E06-S01](../E06-plateforme/E06-S01-fenetre-win32-boucle-messages.md).

## Contexte

Ce ticket doit être fait **tôt**, avant presque tout le reste : c'est lui qui rend
le projet praticable. Sans machine de test rapide à réinitialiser, chaque
vérification passe par du matériel réel, et le cycle de mise au point devient si
lent qu'il décourage l'expérimentation — exactement au moment où le projet en
demande le plus, notamment sur Glide (E05-S01).

Deux émulateurs conviennent, et tous deux émulent une Voodoo :

- **PCem** et son fork **86Box**, qui émulent des machines de l'époque au niveau
  du composant, avec des cartes 3dfx.

L'émulation Voodoo n'est pas parfaite, et c'est une limite à connaître : elle
suffira pour la mise au point fonctionnelle, pas pour valider les performances.
La validation matériel réel de E09-S04 reste indispensable ; l'émulateur ne la
remplace pas, il la rend rare.

## Objectif

Livrer un environnement de test reproductible : Windows 95 avec carte 3dfx émulée,
installable et réinitialisable rapidement.

## Périmètre

**Dans :** la machine émulée, sa configuration, le transfert de fichiers, la
recette.

**Hors :** le matériel réel (E09-S04).

## Travail

1. Choisir l'émulateur en comparant sur un critère concret : la qualité de
   l'émulation Voodoo, et la présence des pilotes Glide.
2. Configurer une machine conforme à l'ADR de E00-S05 : processeur, mémoire, carte
   3dfx, carte son, disque.
3. Installer Windows 95 OSR2.5, les pilotes 3dfx, DirectX, et la version de
   `msvcrt` retenue par E01-S03.
4. Sauvegarder l'état comme image de référence, restaurable en quelques secondes.
   C'est la propriété la plus importante de cet environnement : un plantage de
   Glide en plein écran peut laisser le système inutilisable, et sans restauration
   rapide, chaque essai raté coûte une réinstallation.
5. Automatiser le transfert de fichiers entre l'hôte de développement et la machine
   émulée. Un disque virtuel monté des deux côtés est le moyen le plus simple. Ce
   point décide de la vitesse du cycle de mise au point : il mérite qu'on y passe
   du temps.
6. Écrire un script qui construit, transfère et lance en une commande.
7. Préparer plusieurs configurations : Voodoo 1 à une TMU, Voodoo 2 à deux TMU,
   et une machine plus lente, pour éprouver les replis de E05-S04 et les limites
   de performance.
8. Documenter la recette dans `docs/TEST-ENVIRONMENT.md`, assez précisément pour
   qu'un tiers puisse reconstruire l'environnement.

## Critères d'acceptation

- [ ] Une machine Windows 95 avec 3dfx émulée démarre et exécute une démonstration
      Glide.
- [ ] L'état de référence se restaure en quelques secondes.
- [ ] Le transfert de fichiers est automatisé.
- [ ] Une commande construit, transfère et lance.
- [ ] Au moins trois configurations distinctes sont disponibles.
- [ ] `docs/TEST-ENVIRONMENT.md` permet à un tiers de reconstruire l'environnement.
- [ ] Les limites de l'émulation Voodoo sont documentées, en particulier ce qui ne
      peut pas y être validé.

## Risques

L'émulation Voodoo est approximative sur certains points, et il serait coûteux de
poursuivre un défaut qui n'existe que dans l'émulateur. D'où l'importance de
documenter ses limites connues, et de confronter au matériel réel (E09-S04) à
intervalles réguliers plutôt qu'une seule fois à la fin.

## Références

- PCem · 86Box — émulation de machines d'époque avec cartes 3dfx
- E00-S05 — configuration matérielle cible
- E09-S04 — validation sur matériel réel
