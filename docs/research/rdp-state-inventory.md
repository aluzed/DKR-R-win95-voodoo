# Inventaire de l'état RDP — état au 14 août 2026

> **Cet inventaire est incomplet, et volontairement.** Il consigne ce qui est
> établi et nomme ce qui ne l'est pas. Un inventaire plausible serait plus
> dangereux qu'un inventaire partiel : une configuration manquée ne se voit pas
> au décodage, elle se voit à l'écran, sous forme d'une surface d'une couleur
> inattendue, éventuellement dans un seul niveau.

## Ce qui est établi

### Le décodage est vérifié contre les en-têtes de la décomposition

Les 63 macros `G_CC_*` des en-têtes du portage natif voisin sont résolues,
encodées avec les macros `GCCc*w*` réelles, puis décodées : **les 63 se
retrouvent champ pour champ**, sur les deux cycles.

Les vecteurs sont générés par `tools/win95/gen_combiner_vectors.py` et non
transcrits à la main. La distinction est ce qui donne sa valeur au test : une
transcription se trompe silencieusement, et le test partagerait alors l'erreur du
code qu'il vérifie.

### Un piège que le générateur a révélé, et qui vaut pour E05-S03

**Les largeurs de champ ne sont pas uniformes, et cela change la valeur.**

| Champ | Largeur | `G_CCMUX_0` y vaut |
|---|---:|---:|
| `a`, `b` (RGB) | 4 bits | **15** |
| `c` (RGB) | 5 bits | **31** |
| `d` (RGB) | 3 bits | **7** |
| alpha | 3 bits | **7** |

`G_CCMUX_0` est défini à 31. Il tient dans les cinq bits de `c`, pas dans les
quatre de `a` — le matériel y range 15. Une première version du générateur
émettait la valeur brute et le test accusait le décodeur, **qui avait raison**.

Conséquence à retenir : le même `0` symbolique ne s'encode pas de la même façon
selon la position. Une table de correspondance écrite sur les noms plutôt que sur
les valeurs se tromperait.

### La forme canonique

Une clé de 64 bits, champs à positions fixes, sans compression : deux
configurations distinctes ne peuvent pas collisionner — vérifié sur les 63 —
et une clé se relit à la main.

**Le mode de cycle en fait partie**, et ce n'est pas un détail : le même mot de
combineur en un cycle et en deux ne produit pas la même image, le second étage
n'étant pas évalué dans le premier cas. Les confondre donnerait une table qui
rend la mauvaise image sans jamais se plaindre.

### La traduction s'annonce quand elle est approchée

L'état abstrait de E04-S01 a quatre modes de combinaison ; le RDP en a une
infinité. La traduction rend le mode le plus proche et pose `exact = 0` dans deux
cas : **deux texels lus** — qui demandent deux TMU ou une seconde passe — et
**le double cycle**.

Une traduction approchée qui ne s'annonce pas produit une image plausible et
fausse. C'est le pire résultat possible pour un portage dont l'oracle est
l'image.

## Ce qui n'est pas établi

### L'inventaire des 33 configurations n'est pas revérifié ici

Le ticket demande de le revérifier sur ce portage — « l'inventaire est un point
de départ solide, pas une vérité importée ». **Ce n'est pas possible par la même
méthode.**

Le portage voisin l'a dérivé des **sources C de la décomposition**, en résolvant
les macros des tables de réglages. Ce portage-ci n'a pas ces sources : il
travaille depuis du MIPS recompilé, et `extern/dkr-decomp` est absent. Son
équivalent est l'instrumentation à l'exécution — l'étape 6 du ticket — qui
demande une partie complète, donc la ROM.

Le rappel du voisin, pour mémoire :

| | Configurations | Entrées |
|---|---:|---:|
| depuis les tables de réglages | 21 | 193 |
| émises hors des tables | 12 | 22 |
| **total consolidé** | **33** | |

| Texels lus | Configurations | Conséquence sur la cible |
|---:|---:|---|
| 0 | 8 | aucune texture — 1 TMU inutilisée |
| 1 | 22 | 1 TMU suffit, une seule passe |
| 2 | **3** | 2 TMU, ou une seconde passe sur mono-TMU |

Les trois à deux texels sont `BLENDTEX_PRIM`/`MODULATEIDECALA2` (32 entrées,
lumières clignotantes), `BLENDT_ENV_ALPHA_A_T1xP`/`PASS2` (police) et
`BLENDTEX_MODULATEA_1_PRIM`/`BLENDI_ENV_ALPHA_MODULATEA2` (vagues).

### La table des configurations connues est amorcée, pas complète

`rdp_state.c` répertorie huit configurations dont la composition exacte est
connue. `dkr_rdp_combiner_name` rend `NULL` pour tout le reste, et **c'est ce
signalement qui compte** en attendant l'instrumentation.

### Fréquence et surface d'écran : absentes

Le critère demande, par configuration, la fréquence et la surface d'écran
couverte — ce qui décide de l'ordre de traitement en E05-S03. Les deux se
mesurent à l'exécution. Le nombre d'entrées de table du voisin en est un
substitut grossier : il compte des déclarations, pas des pixels.

## Modes décodés hors combineur

| Champ | Source | Décodé |
|---|---|---|
| mode de cycle | `othermode_h` bit 20, 2 bits | 1 cycle, 2 cycles, copy, fill |
| filtrage | `othermode_h` bit 12, 2 bits | point (0), bilerp (2), average (3) |
| LOD, détail, perspective de texture | `othermode_h` 16, 17, 19 | oui |
| comparaison alpha | `othermode_l` bit 0, 2 bits | oui |
| source de Z | `othermode_l` bit 2 | oui |
| test et écriture de profondeur | `othermode_l` bits 4 et 5 | oui |
| blender | `othermode_l` >> 3 | conservé brut — relève de E05-S05 |

`G_TF_AVERAGE` vaut 3 et **il n'y a pas de valeur 1** : traiter le champ comme un
booléen donnerait le filtrage bilinéaire pour `AVERAGE`, ce qui est presque juste
et donc difficile à voir. Le décodeur distingue, et la suite le vérifie.
