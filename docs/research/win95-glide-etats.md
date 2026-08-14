# Les constantes de Glide 2.x, vérifiées par relecture

Mesuré le 14 août 2026 sur la machine d'épreuve (Pentium II 400, Voodoo 2,
Windows 95 OSR2), par `tools/win95/witnesses/glide_state_probe.c`.

## Pourquoi vérifier des constantes

Il n'y a pas de `glide.h` sur cette machine : le pilote 3dfx livre `glide2x.dll`
sans son en-tête. Les valeurs d'énumération employées par `glide_backend.c`
viennent de la spécification Glide 2.4, écrites de mémoire.

Le danger n'est pas qu'une valeur fausse provoque une erreur. **Glide ne valide
pas ses énumérations** : elle programme le registre correspondant et rend la
main. L'image sort différente, sans le moindre signal, et l'écart est ensuite
attribué au décodeur de display list ou au transformateur — c'est-à-dire aux
deux modules qu'on relira en premier et où il n'y a rien à trouver.

La relecture du tampon d'image (`grLfbLock`, voir `win95-glide-amorcage.md`)
permet de trancher : on dessine une figure dont on connaît la réponse, on lit un
pixel, on compare à une valeur calculée à la main.

## Le résultat

| Ce qui est exercé | Attendu | Lu | |
|---|---|---|---|
| Mélange opaque | `0xFF0000` | `0xFF0000` | CONFIRME |
| Mélange alpha, α=128 sur noir | `0x800000` | `0x7B0000` | CONFIRME |
| Mélange additif, rouge sur bleu | `0xFF0040` | `0xFF0042` | CONFIRME |
| Profondeur, proche après lointain | rouge | rouge | CONFIRME |
| Profondeur, lointain après proche | rouge | rouge | CONFIRME |
| Ciseaux, moitié droite | ~56000 px | 55960 px | CONFIRME |
| Test alpha, α=64 < réf 128 | 0 px | 0 px | CONFIRME |
| Test alpha, α=200 ≥ réf 128 | 112000 px | 112000 px | CONFIRME |

Les écarts de couleur (`0x7B` pour `0x80`, `0x42` pour `0x40`) sont la
quantification 565 : cinq bits de rouge et de bleu, six de vert. Ils sont dans la
tolérance et ne signalent rien.

Le brouillard et le mode de faces arrière ne figurent pas : le premier n'est pas
encore exercé par une scène qui le rende mesurable, le second est délibérément
désactivé sur la carte — voir plus bas.

## Les deux chiffres qui valent une preuve

Le triangle d'épreuve a pour sommets (320,40), (600,440) et (40,440). Son aire
analytique vaut ½ × 560 × 400 = **112000 pixels**, et c'est exactement le compte
relevé au test alpha. La carte remplit donc la surface géométrique sans
débordement ni manque, et sa règle de remplissage des bords ne compte chaque
pixel qu'une fois.

Le triangle étant symétrique autour de x = 320, une fenêtre de ciseaux limitée à
la moitié droite doit en garder 56000. Relevé : 55960, soit 40 pixels de moins —
la colonne frontière, comptée une fois. La fenêtre de Glide est donc bien
**incluse à gauche, exclue à droite**, ce que `backend.h` supposait.

## Le tampon de profondeur : la faute que le témoin a attrapée

C'est le seul point où la mémoire était fausse, et il valait le détour.

Le raisonnement naturel est le suivant. Le sommet porte `oow = 1/w` ; un objet
proche a un `w` petit, donc un `1/w` **grand** ; en tampon w, le proche doit donc
gagner avec `GR_CMP_GREATER`. C'est ce qui avait été écrit.

L'écran est resté **entièrement noir**, dans les deux ordres de dessin. C'est ce
« dans les deux ordres » qui a permis de conclure : un défaut de tri aurait
donné le mauvais triangle, pas l'absence de triangle.

Le raisonnement oublie une étape. Glide ne range pas `1/w` dans le tampon : elle
y range une valeur w encodée qui **croît avec la distance**, et `grBufferClear`
l'efface à `GR_WDEPTHVALUE_FARTHEST` = `0xFFFF`. Rien ne pouvant dépasser ce
maximum, `GR_CMP_GREATER` rejette la totalité de la scène — y compris le premier
triangle, qui n'a pourtant rien devant lui.

La comparaison correcte est `GR_CMP_LESS`, comme en tampon z.

Le symptôme méritait d'être consigné : un écran noir se diagnostique d'abord
comme un défaut de géométrie, de fenêtre ou de matrice, et l'on inspecte
longtemps une chaîne de transformation parfaitement juste avant de soupçonner un
tampon de profondeur qui fonctionne, lui aussi, parfaitement.

## Ce qui est délibérément non traduit

**Les faces arrière.** `apply_cull` désactive toujours `grCullMode`. La chaîne
élimine déjà elle-même (`dkr_cull_accept`), parce que le rastériseur de référence
doit écarter *les mêmes* triangles que la carte — sans quoi la comparaison de
E09-S02 mesurerait une différence de convention plutôt qu'une différence de
rendu. Programmer aussi la carte élimineraient deux fois, selon deux conventions
qui ne coïncident pas nécessairement, et le risque est de tout vider.

**Les textures.** `texture_upload` rend zéro. L'allocation en mémoire de TMU est
E05-S02, la traduction du combineur RDP est E05-S03. En attendant, les quatre
modes de combineur retombent sur la couleur du sommet : sélectionner une texture
absente donnerait du blanc, c'est-à-dire un écran faux *d'une manière qui
ressemble à un défaut de combineur*. Un rendu franchement non texturé se
diagnostique mieux.

## Le témoin s'est lui-même trompé une fois

La première version comptait les pixels peints après la boucle de cadence, qui
efface sur un dégradé : les 307200 pixels étaient « peints » et le verdict était
positif sans rien établir. Il dessine désormais une dernière image sur fond noir
avant de lire.

La leçon dépasse Glide : **un compteur dont la valeur de fond n'est pas
distinguable du résultat ne mesure rien**, et il est d'autant plus dangereux
qu'il affiche un succès.
