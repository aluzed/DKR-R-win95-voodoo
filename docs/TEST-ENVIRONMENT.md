# Environnement de test émulé

Recette de la machine Windows 95 / 3dfx sur laquelle le portage se met au point.
Livrable de [E09-S01](stories/E09-qa/E09-S01-environnement-test-emule.md).

## Pourquoi cet environnement passe avant le reste

Le cycle « modifier, exécuter, observer » du portage passe par une machine
Windows 95 avec une carte 3dfx. Sur du matériel réel, chaque itération coûte une
copie de fichier, un redémarrage, et — quand Glide plante en plein écran — une
réinstallation. L'émulateur ramène ce cycle à quelques secondes, et c'est ce qui
rend praticable le tâtonnement inévitable de [E05-S01](stories/E05-glide/E05-S01-initialisation-glide-buffers.md).

Il ne remplace pas la validation sur matériel réel
([E09-S04](stories/E09-qa/E09-S04-validation-materiel-reel.md)) : il la rend rare.

## Montage

```bash
scripts/Setup-Win95-TestVM.sh
```

Le script n'installe rien à l'échelle du système — tout va dans
`~/.local/dkr-win95` — et n'a pas besoin de droits root. Il récupère 86Box, le
jeu de BIOS, installe `mtools` localement, crée les images disque et écrit la
configuration machine.

| Élément | Emplacement |
|---|---|
| 86Box v6.0 | `~/.local/dkr-win95/opt/86box/squashfs-root/AppRun` |
| Jeu de BIOS | `~/.local/dkr-win95/opt/86box/roms` |
| Machine | `~/.local/dkr-win95/vm/dkr-p2-voodoo2` |

L'AppImage est **extraite** plutôt que montée : FUSE est souvent absent des
postes de build, et l'extraction supprime cette dépendance.

## Préparation de l'installation

**Il faut fournir son propre média Windows 95 OSR2.5** — logiciel propriétaire de
Microsoft, que les scripts ne téléchargent pas et ne peuvent pas télécharger.

```bash
scripts/prepare_win95_install.py --iso /chemin/vers/W95.iso
```

Ce script crée les images disque, les **partitionne** et les formate, puis y
recopie la source d'installation extraite de l'ISO :

| Disque | Image | Contenu |
|---|---|---|
| A: | `freedos-boot.img` | démarrage FreeDOS 1.4 |
| C: | `win95.img` — 1 023 Mio | système, à partitionner puis installer |
| D: | `transfer.img` — 504 Mio | transfert hôte ↔ invité |
| E: | `install.img` — 128 Mio | source Windows 95, 46 Mio |

Deux points méritent d'être connus, parce qu'ils sont la cause habituelle des
échecs à cette étape :

- **Un disque dur DOS exige une table de partition.** Un volume FAT écrit
  directement au début de l'image, sans MBR, n'est pas vu par DOS. Le script
  écrit donc un MBR et place le système de fichiers à partir du secteur 63.
- **La source est sur un disque dur, pas sur le CD.** Cela supprime toute
  dépendance à un pilote CD-ROM sous DOS, qui est le point le plus fragile d'une
  installation Windows 95 en émulation.

Le CD OSR2.5 français ne contient pas de `SETUP.EXE` : son programme
d'installation s'appelle **`INSTALL.EXE`** (exécutable NE Windows 3.x, qui charge
`DOSSETUP.BIN` puis `WINSETUP.BIN`). L'ISO n'est pas amorçable — elle n'a pas de
descripteur El Torito — d'où la disquette FreeDOS.

## Installation

C: est déjà partitionné, formaté et marqué actif : **ni `FDISK` ni `FORMAT` ne
sont nécessaires**, et le redémarrage que `FDISK` impose entre les deux est
évité. La disquette FreeDOS lance directement `E:\WIN95\INSTALL.EXE`.

```bash
scripts/Run-Win95-VM.sh          # sur un poste avec écran
```

Au tout premier démarrage, le BIOS affiche :

```text
CMOS checksum error - Defaults loaded
Press F1 to continue, DEL to enter SETUP
```

C'est normal — la CMOS est vierge. Appuyer sur **F1**. Le message ne réapparaît
plus une fois la machine arrêtée proprement.

Puis installer les pilotes 3dfx pour Voodoo 2 dans l'invité.

## Pilotage sans écran

Le poste de développement n'a pas forcément de session graphique. `Drive-Win95-VM.sh`
fait tourner 86Box sur un affichage X virtuel, capture l'écran et injecte des
touches — ce qui rend l'environnement pilotable depuis un terminal, et fournit le
socle du harnais de comparaison visuelle de
[E09-S02](stories/E09-qa/E09-S02-harnais-comparaison-visuelle.md).

```bash
scripts/Drive-Win95-VM.sh start                  # Xvfb + 86Box + capture clavier
scripts/Drive-Win95-VM.sh key F1                 # passer l'avertissement CMOS
scripts/Drive-Win95-VM.sh key Return             # menu de langue FreeDOS
scripts/Drive-Win95-VM.sh shot ecran.png         # voir où en est la machine
scripts/Drive-Win95-VM.sh type "DIR E:\WIN95"
scripts/Drive-Win95-VM.sh stop
```

Trois pièges coûtent chacun une bonne heure si on ne les connaît pas :

- **`xdotool key --window` ne marche pas.** Il passe par `XSendEvent`, que Qt
  ignore. Il faut poser le focus X puis utiliser XTEST, c'est-à-dire `xdotool
  key` *sans* `--window`.
- **86Box ne route le clavier vers la machine émulée qu'après un clic** dans sa
  fenêtre, qui capture les périphériques. Sans ce clic, les touches vont à
  l'interface de l'émulateur. `Drive-Win95-VM.sh start` fait ce clic ;
  `grab` le refait si la capture a été relâchée.
- **L'invité a sa propre disposition de clavier, et elle s'applique aux
  scancodes.** `xdotool type "24796"` sur un Windows français produit `é'èç-` :
  l'hôte envoie les touches de la rangée du haut, et l'invité les interprète en
  AZERTY. Deux conséquences pratiques :

  | Ce qu'on veut | Ce qu'il faut envoyer |
  |---|---|
  | un chiffre | `shift+<chiffre>` — en AZERTY la rangée du haut est en majuscule |
  | `A Q Z W M` | les touches croisées ; à éviter dans les chaînes de test |
  | le pavé numérique | inutilisable, NumLock est éteint dans l'invité |

  Le plus sûr, pour un texte libre, est de n'employer que des lettres identiques
  dans les deux dispositions.

Le diagnostic par capture d'écran est ce qui a permis de trouver l'attente sur
`Press F1` : de l'extérieur, une machine bloquée au BIOS et une machine qui
n'amorce pas sont indiscernables.

Une fois Windows et les pilotes 3dfx installés, figer l'état :

```bash
scripts/Run-Win95-VM.sh --snapshot
```

et y revenir après chaque essai qui tourne mal :

```bash
scripts/Run-Win95-VM.sh --restore
```

Cette restauration est la propriété la plus importante de l'environnement. Une
carte 3dfx en mode *passthrough* prend le contrôle de l'écran ; un plantage au
mauvais moment laisse l'invité inutilisable, et sans retour rapide à un état
sain, chaque essai raté coûterait une réinstallation complète.

## Configuration de la machine

Conforme à la cible du projet, sous réserve de l'ADR 0002
([E00-S05](stories/E00-cadrage/E00-S05-adr-cible-materielle-glide.md)) :

| Élément | Valeur | Remarque |
|---|---|---|
| Carte mère | Asus P2B-LS (`p2bls`) | chipset 440BX, la plate-forme Pentium II de référence |
| Processeur | Pentium II Deschutes, 400 MHz | |
| Mémoire | 64 Mo | |
| Vidéo 2D | S3 ViRGE/DX | la Voodoo 2 n'a pas de sortie 2D |
| Vidéo 3D | 3dfx Voodoo 2 | 4 Mo de tampon d'image, 4 Mo de texture |
| Son | Sound Blaster 16 | |
| Disques | 2 × IDE | C: système, D: transfert |

Le choix d'une 2D séparée n'est pas un artifice d'émulation : c'est le montage
réel d'une Voodoo 2, qui se branche en sortie de la carte 2D et prend la main
seulement en 3D plein écran.

Chaque valeur est surchargeable par variable d'environnement, ce qui permet de
préparer les autres configurations exigées par le ticket :

```bash
# Voodoo 1, une seule TMU — éprouve le repli multipasse de E05-S04
DKR_WIN95_VM=dkr-p1-voodoo1 DKR_WIN95_VOODOO_TYPE=0 \
DKR_WIN95_VOODOO_TEX=2 DKR_WIN95_VOODOO_FB=2 \
  scripts/Setup-Win95-TestVM.sh

# Machine basse — éprouve le plancher de performance
DKR_WIN95_VM=dkr-p2-slow DKR_WIN95_CPU_SPEED=233000000 DKR_WIN95_MEM_KB=32768 \
  scripts/Setup-Win95-TestVM.sh
```

## Transfert de fichiers

```bash
scripts/Push-To-Win95-VM.sh --dir DKRTEST build/DKR-R.exe
```

Le fichier apparaît en `D:\DKRTEST\` dans l'invité. L'écriture passe par
`mtools` directement dans l'image FAT16, sans monter quoi que ce soit et sans
droits root.

Deux points de vigilance :

- **La machine doit être arrêtée** au moment où l'invité doit voir le résultat.
  Windows 95 met le volume en cache et ne relira pas une image modifiée sous
  lui.
- **FAT16 impose le 8.3.** Le script prévient quand un nom sera tronqué. C'est
  la même contrainte que celle qui pèse sur les fichiers de sauvegarde
  ([E02-S05](stories/E02-systeme/E02-S05-sauvegardes-eeprom-controller-pak.md)).

## Limites connues

Ce qui suit **ne peut pas** être validé ici, et doit passer par E09-S04 :

| Limite | Conséquence |
|---|---|
| L'émulation Voodoo est fonctionnelle, pas temporelle | aucune mesure de performance graphique n'est transposable |
| L'hôte exécute le rendu bien plus vite que le matériel d'époque | le budget de remplissage ne se mesure pas ici |
| Ni bande passante PCI, ni latence de disque d'époque | téléchargements de texture (E05-S02) et temps de chargement (E02-S04) sont optimistes |
| Pilotes 3dfx réels, cartes son et manettes du commerce | compatibilité à vérifier sur matériel |

En revanche, tout ce qui est **fonctionnel** se valide ici : format du binaire,
imports PE, démarrage, threads, décodage de la display list, justesse du rendu,
sauvegardes, entrées.

## État actuel

| Étape | État |
|---|---|
| 86Box installé et exécutable, sans droits root | ✅ |
| Machine POST : Pentium II 400 MHz, 65 536 Ko, 3 disques détectés | ✅ |
| Images disque partitionnées et formatées depuis l'hôte | ✅ |
| Source Windows 95 extraite de l'ISO vers E: | ✅ 63 fichiers, 46 Mio |
| Amorçage FreeDOS, C:/D:/E: visibles par DOS | ✅ |
| Transfert hôte ↔ invité | ✅ dans les deux sens |
| Pilotage sans écran (capture + injection de touches) | ✅ |
| **Windows 95 OSR2.5 installé et démarre** | ✅ |
| Sound Blaster 16 détectée par Windows | ✅ |
| Instantané de référence | ✅ `Run-Win95-VM.sh --snapshot` |
| **Pilote 3dfx 3.01.00 installé et lié à la carte** | ✅ « Voodoo2 3D Accelerator », sans avertissement |
| **Runtime Glide en place** | ✅ `glide2x.dll`, `glide3x.dll`, `fxmemmap.vxd` dans `C:\WINDOWS\SYSTEM` |
| **Démonstration Glide** | ✅ contexte 640×480, effacements, échanges de tampons, triangle Gouraud |

### Vérifier le modèle de carte — le piège le plus coûteux

**86Box n'a pas appliqué les réglages Voodoo écrits à la main dans `86box.cfg`.**
Le fichier disait `type = 1`, `framebuffer_memory = 4`, `texture_memory = 4` ; le
dialogue de réglages affichait « Graphique 3dfx Voodoo », 2 Mo + 2 Mo. La machine
a donc émulé une **Voodoo 1** pendant toute l'installation, en conservant
fidèlement le texte de la configuration.

Rien ne le signale, sauf en cherchant : le POST liste la carte en `121A 0001`
(l'identifiant de la Voodoo 1), et Glide finit par répondre :

```text
_GlideInitEnvironment: glide2x.dll expected Voodoo, none detected
```

C'est aussi la vraie raison pour laquelle `voodoo2.inf` ne reconnaissait pas la
carte : elle n'était effectivement pas une Voodoo 2.

**Vérifier une fois par le dialogue**, avant toute mesure :

```bash
86Box -S    # Affichage → Graphique Voodoo 1 ou 2 → Configurer
```

et y choisir « 3Dfx Voodoo 2 », 4 Mo de tampon d'images, 4 Mo de textures. Après
ce passage, les mêmes valeurs dans le fichier sont honorées, et Windows redétecte
un nouveau matériel au démarrage suivant.

Conséquence pour [E00-S05](stories/E00-cadrage/E00-S05-adr-cible-materielle-glide.md) :
**le modèle de carte émulé doit être vérifié dans le dialogue, pas déduit du
fichier de configuration.** Une mesure de budget de texture faite sur 2 Mo au
lieu de 4, ou un test de multitexture fait sur une seule TMU, serait faux sans
que rien ne le signale.

### Le pilote 3dfx : deux pièges

**L'assistant de mise à jour de pilote n'offre pas « Disquette fournie ».** Il
filtre les modèles par la classe du périphérique existant, et un périphérique
« inconnu » ne correspond à aucune classe : il ne propose que « Périphérique non
pris en charge ». Le bouton se trouve dans **Panneau de configuration → Ajout de
périphérique**, qui fait lire l'INF directement.

**86Box expose sa Voodoo 2 avec l'identifiant PCI de la Voodoo 1.** Le POST le
montre : `121A 0001` dans la colonne Device ID, alors que `voodoo2.inf` ne se lie
qu'à `PCI\VEN_121A&DEV_0002`. Le pilote d'origine ne peut donc pas reconnaître la
carte émulée, et l'auto-détection échoue quel que soit le chemin indiqué.

Correction : ajouter la liaison `DEV_0001` à côté de celle d'origine, aux trois
endroits où l'INF la déclare (`[Mfg]`, la clé `Enum`, et les chaînes de
description). `scripts/patch_voodoo2_inf.py` le fait.

À retenir pour [E05-S01](stories/E05-glide/E05-S01-initialisation-glide-buffers.md) :
la détection de carte à l'exécution ne doit pas se fier au seul identifiant PCI,
puisqu'il ment sur cette plate-forme de test. C'est `grGet` /
`grSstQueryBoards` qui font foi.

### Installer le pilote 3dfx

Les fichiers sont extraits dans `C:\WINDOWS\TEMP` par le paquet
`V2_W9X_3.EXE`, puis l'INF est corrigé et installé :

```bash
scripts/patch_voodoo2_inf.py --inf voodoo2.inf --output VOODOO2.INF
```

Dans l'invité : **Panneau de configuration → Ajout de périphérique → Non →
Contrôleurs son, vidéo et jeux → Disquette fournie → `C:\WINDOWS\TEMP` →
Voodoo2 3D Accelerator**, puis redémarrer.

Vérification depuis l'hôte, machine arrêtée :

```bash
mdir -i win95.img@@32256 ::/WINDOWS/SYSTEM | grep -iE 'glide|fxmemmap'
```

`fxmemmap.vxd` est le pilote noyau qui mappe les registres de la carte ; sans
lui, `glide2x.dll` se charge mais n'ouvre aucun contexte.

## Limites connues

Ce qui suit **ne peut pas** être validé ici, et doit passer par E09-S04 :

| Limite | Conséquence |
|---|---|
| L'émulation Voodoo est fonctionnelle, pas temporelle | aucune mesure de performance graphique n'est transposable |
| L'hôte exécute le rendu bien plus vite que le matériel d'époque | le budget de remplissage ne se mesure pas ici |
| Ni bande passante PCI, ni latence de disque d'époque | téléchargements de texture (E05-S02) et temps de chargement (E02-S04) sont optimistes |
| Pilotes 3dfx réels, cartes son et manettes du commerce | compatibilité à vérifier sur matériel |

En revanche, tout ce qui est **fonctionnel** se valide ici : format du binaire,
imports PE, démarrage, threads, décodage de la display list, justesse du rendu,
sauvegardes, entrées.

## État actuel

| Étape | État |
|---|---|
| 86Box installé et exécutable, sans droits root | ✅ |
| Machine POST : Pentium II 400 MHz, 65 536 Ko, 3 disques détectés | ✅ |
| Images disque partitionnées et formatées depuis l'hôte | ✅ |
| Source Windows 95 extraite de l'ISO vers E: | ✅ 63 fichiers, 46 Mio |
| Amorçage FreeDOS, C:/D:/E: visibles par DOS | ✅ |
| Transfert hôte ↔ invité | ✅ dans les deux sens |
| Pilotage sans écran (capture + injection de touches) | ✅ |
| **Windows 95 OSR2.5 installé et démarre** | ✅ |
| Sound Blaster 16 détectée par Windows | ✅ |
| Instantané de référence | ✅ `Run-Win95-VM.sh --snapshot` |
| **Pilote 3dfx 3.01.00 installé et lié à la carte** | ✅ « Voodoo2 3D Accelerator », sans avertissement |
| **Runtime Glide en place** | ✅ `glide2x.dll`, `glide3x.dll`, `fxmemmap.vxd` dans `C:\WINDOWS\SYSTEM` |
| **Démonstration Glide** | ✅ contexte 640×480, effacements, échanges de tampons, triangle Gouraud |

### Vérifier le modèle de carte — le piège le plus coûteux

**86Box n'a pas appliqué les réglages Voodoo écrits à la main dans `86box.cfg`.**
Le fichier disait `type = 1`, `framebuffer_memory = 4`, `texture_memory = 4` ; le
dialogue de réglages affichait « Graphique 3dfx Voodoo », 2 Mo + 2 Mo. La machine
a donc émulé une **Voodoo 1** pendant toute l'installation, en conservant
fidèlement le texte de la configuration.

Rien ne le signale, sauf en cherchant : le POST liste la carte en `121A 0001`
(l'identifiant de la Voodoo 1), et Glide finit par répondre :

```text
_GlideInitEnvironment: glide2x.dll expected Voodoo, none detected
```

C'est aussi la vraie raison pour laquelle `voodoo2.inf` ne reconnaissait pas la
carte : elle n'était effectivement pas une Voodoo 2.

**Vérifier une fois par le dialogue**, avant toute mesure :

```bash
86Box -S    # Affichage → Graphique Voodoo 1 ou 2 → Configurer
```

et y choisir « 3Dfx Voodoo 2 », 4 Mo de tampon d'images, 4 Mo de textures. Après
ce passage, les mêmes valeurs dans le fichier sont honorées, et Windows redétecte
un nouveau matériel au démarrage suivant.

Conséquence pour [E00-S05](stories/E00-cadrage/E00-S05-adr-cible-materielle-glide.md) :
**le modèle de carte émulé doit être vérifié dans le dialogue, pas déduit du
fichier de configuration.** Une mesure de budget de texture faite sur 2 Mo au
lieu de 4, ou un test de multitexture fait sur une seule TMU, serait faux sans
que rien ne le signale.

### Le pilote 3dfx : deux pièges

**L'assistant de mise à jour de pilote n'offre pas « Disquette fournie ».** Il
filtre les modèles par la classe du périphérique existant, et un périphérique
« inconnu » ne correspond à aucune classe : il ne propose que « Périphérique non
pris en charge ». Le bouton se trouve dans **Panneau de configuration → Ajout de
périphérique**, qui fait lire l'INF directement.

**86Box expose sa Voodoo 2 avec l'identifiant PCI de la Voodoo 1.** Le POST le
montre : `121A 0001` dans la colonne Device ID, alors que `voodoo2.inf` ne se lie
qu'à `PCI\VEN_121A&DEV_0002`. Le pilote d'origine ne peut donc pas reconnaître la
carte émulée, et l'auto-détection échoue quel que soit le chemin indiqué.

Correction : ajouter la liaison `DEV_0001` à côté de celle d'origine, aux trois
endroits où l'INF la déclare (`[Mfg]`, la clé `Enum`, et les chaînes de
description). `scripts/patch_voodoo2_inf.py` le fait.

À retenir pour [E05-S01](stories/E05-glide/E05-S01-initialisation-glide-buffers.md) :
la détection de carte à l'exécution ne doit pas se fier au seul identifiant PCI,
puisqu'il ment sur cette plate-forme de test. C'est `grGet` /
`grSstQueryBoards` qui font foi.

### Ce qu'il restait : lier le pilote 3dfx

Les fichiers du pilote 3.01.00 sont déjà extraits dans `C:\WINDOWS\TEMP` —
`voodoo2.inf`, `glide2x.dll`, `glide3x.dll`, `3dfxv2.drv`, `fxmemmap.vxd`. La
carte apparaît dans le gestionnaire de périphériques sous **Autres périphériques
→ PCI Multimedia Video Device**, sans pilote.

L'assistant de mise à jour de pilote **ne reconnaît pas l'INF** : il répond
« l'emplacement sélectionné ne contient pas de pilote mis à jour » et, en
sélection manuelle, ne propose que « Périphérique non pris en charge » sans
bouton « Disquette fournie ». Trois voies ont été essayées sans succès :
recherche automatique, « Autres emplacements » puis *Terminer* (la procédure que
le readme du pilote prescrit pour OSR2), et `rundll32
setupx.dll,InstallHinfSection`.

La voie qui reste, et qui expose le bouton « Disquette fournie » absent de
l'assistant de mise à jour :

```text
Démarrer → Paramètres → Panneau de configuration → Ajout de nouveau matériel
  → Suivant
  → « Non » (ne pas rechercher automatiquement)
  → choisir le type de matériel
  → « Disquette fournie... » → C:\WINDOWS\TEMP
  → « Voodoo2 3D Accelerator »
```

Cette manipulation prend une minute sur un poste avec écran
(`scripts/Run-Win95-VM.sh`). Elle est aussi pilotable par
`scripts/Drive-Win95-VM.sh`, au prix d'une navigation clavier plus longue.

### Démonstration Glide

`tools/win95/glidetest.c` ouvre un contexte Glide 640×480, enchaîne trois
effacements avec échange de tampons, puis dessine un triangle Gouraud. Il écrit
son déroulé dans `D:\GLIDETST.TXT`, lisible depuis l'hôte — une preuve
indépendante de toute capture d'écran.

```bash
tools/win95/build-glidetest.sh              # PE 32 bits, sans CRT, sans SSE
scripts/Push-To-Win95-VM.sh tools/win95/GLIDETST.EXE
# dans l'invité : d:\glidetst.exe
```

Le binaire n'importe que `kernel32` et `user32`, et charge Glide par
`LoadLibrary` : il ne dépend d'aucun redistribuable et écarte le démarrage du CRT
de mingw-w64, qui est le point d'achoppement attendu sous Windows 95. À ce titre
il sert aussi de premier témoin pour
[E00-S02](stories/E00-cadrage/E00-S02-spike-toolchain-pe-win95.md).

Résultat obtenu :

```text
glide2x.dll charge
symboles Glide resolus
grGlideInit
cartes 3dfx detectees : 1
contexte 640x480 ouvert, double buffer
grBufferClear + grBufferSwap x3
grDrawTriangle : triangle Gouraud
SUCCES : la pile Glide fonctionne de bout en bout
```

**Un piège trouvé par cette démo, et qui vaut pour tout E05 :** la structure
`GrVertex` de Glide 2.x range ses champs dans l'ordre `x, y, z, r, g, b, ooz, a,
oow` — `ooz` et `a` s'intercalent entre les couleurs et `oow`. Une structure
« logique » (`x, y, ooz, oow, r, g, b, a`) compile sans un avertissement et rend
un triangle impeccable, **aux couleurs permutées** : Glide lit simplement les
flottants aux mauvais décalages. Un sommet rouge sort vert. Rien dans le code, le
compilateur ou Glide ne le signale — seule la comparaison visuelle l'attrape.
C'est l'argument de [E04-S08](stories/E04-hle-f3ddkr/E04-S08-rasteriseur-logiciel-reference.md)
en miniature.
