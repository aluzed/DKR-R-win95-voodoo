# La machine de test émulait un Voodoo 1, et pourquoi on ne l'avait pas vu

Relevé de [E00-S05](../stories/E00-scoping/E00-S05-adr-hardware-target-glide.md),
14 août 2026.

## Le fichier ne mentait pas — on ne lisait pas la bonne section

L'ADR 0002 concluait que `86box.cfg` « mentait » : Glide rapportait le type `0`
(Voodoo Graphics) alors que le fichier annonçait `type = 2`. C'était la troisième
fois qu'on l'accusait, et c'était faux à chaque fois.

Il y a **deux sections Voodoo** dans ce fichier :

```ini
[3dfx Voodoo Graphics #1]     ← celle que 86Box lit
type = 1

[3Dfx Voodoo Graphics]        ← écrite à la main, jamais lue
type = 2
```

86Box lit celle qui porte le **suffixe d'instance**. Elle disait `type = 1`,
c'est-à-dire *Obsidian SB50 + Amethyst* — un Voodoo 1 à deux TMU — et 86Box
l'honorait fidèlement. Accuser l'outil de mentir a coûté deux mois pendant
lesquels la machine n'était pas celle qu'on croyait.

L'énumération se lit dans le binaire de 86Box :

```
0  3Dfx Voodoo Graphics
1  Obsidian SB50 + Amethyst (2 TMUs)
2  3Dfx Voodoo 2
```

## La machine est maintenant sur la carte plancher

`type = 2`, tampon d'images 2 Mo, textures 2 Mo — la configuration **plancher**
de l'ADR, celle qui met à l'épreuve les deux contraintes les plus serrées.
Confirmé dans le dialogue de réglages, seule source qui fasse foi :

```text
Type de Voodoo                     : 3Dfx Voodoo 2
Taille memoire du tampon d'images  : 2 Mo
Taille memoire des textures        : 2 Mo
```

Le budget de texture de E05-S02 sera donc éprouvé contre la vraie limite, et non
contre le double comme c'était le cas avec 4 Mo par TMU.

## Ce que Glide 2.54 en dit, et ce qu'il n'en dit pas

| | Obsidian (avant) | Voodoo 2 (après) |
|---|---|---|
| `type` | 0 | **0** |
| révision FBI | 261 | **261** |
| mémoire image | 4 Mo | 2 Mo |
| TMU | 2 × 4 Mo | 2 × 2 Mo |

**Seules les tailles mémoire changent.** Le type et la révision FBI sont
identiques.

`GrSstType` de Glide 2.x ne sépare pas les deux générations :
`GR_SSTTYPE_VOODOO` couvre la famille, et `glide2x` 2.54 *est* le pilote
Voodoo 2. Ce n'est donc pas une anomalie de l'émulation mais la forme de l'API.

**Conséquence pour E05-S01 :** la détection à l'exécution ne peut pas reposer sur
le type. Le nombre de TMU et la mémoire par TMU sont exploitables — et ce sont
d'ailleurs les deux seules choses dont le moteur ait besoin pour décider entre
une passe et deux. Le modèle exact ne l'est pas.

**Réserve de portée :** ce relevé est celui de l'émulation. Sur du matériel réel
la révision FBI diffère entre générations et pourrait discriminer — à vérifier en
E09-S04, et une raison de plus de ne pas y faire reposer la détection.

## Reproduire

```sh
P=~/.local/dkr-win95
i686-w64-mingw32-gcc-posix -O2 -march=pentium2 -mno-sse -D_WIN32_WINNT=0x0400 \
  -nostdlib -nostartfiles -e _start -o GLIDEHW.EXE \
  tools/win95/probes/glide_hwinfo.c -lkernel32 -luser32

scripts/Push-To-Win95-VM.sh GLIDEHW.EXE    # puis l'exécuter, lire D:\GLIDEHW.TXT
```

Et **vérifier le modèle dans le dialogue de réglages** : Outils → Réglages →
Affichage → Configurer, en face de « Graphique Voodoo 1 ou 2 ». Jamais dans le
fichier.
