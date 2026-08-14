# Profondeur, test alpha et mélange

Mesuré le 14 août 2026 sur la machine d'épreuve, par
`tools/win95/witnesses/depth_probe.c` et `glide_state_probe.c`.

## Z ou W : la mesure ne les départage pas

Le ticket annonce que « le W offre une bien meilleure répartition de la précision
en profondeur ». La mesure ne le confirme pas.

Scène construite pour être le pire cas — deux surfaces quasi coplanaires, très
loin, dans une plage de piste de course (plan proche 10, plan lointain 20000) :

| Écart | Distance | Tampon W | Tampon Z |
|---|---|---|---|
| 2 ‰ | 5000 | 0 pixel en combat | 0 pixel |
| 2 ‰ | 15000 | 0 pixel | 0 pixel |
| 0,2 ‰ | 15000 | **307200 (tout)** | **307200 (tout)** |

Les deux tampons résolvent deux pour mille à toute distance de la plage, et les
deux cèdent entièrement à deux dixièmes de pour mille. **Le mur est au même
endroit.**

Le cas de saturation a été ajouté exprès : une comparaison sans point de rupture
ne dit pas où est la limite, seulement que les deux candidats passent les cas
faciles. C'est ce cas-là qui établit que le résultat n'est pas « les deux sont
parfaits » mais « les deux ont la même limite ».

Le choix reste donc le tampon en W, **pour une raison qui n'est pas la
précision** : `dkr_render_vertex` porte déjà `oow = 1/w`, que Glide consomme
telle quelle en mode W. Le mode Z lit `ooz` sur [0, 65535] alors que la chaîne
produit une profondeur sur [0, 1], et il faudrait remettre chaque sommet à
l'échelle — donc les recopier, donc perdre le bénéfice d'avoir calqué `GrVertex`
champ pour champ.

## Un piège de mesure qui a failli inverser la conclusion

La première mesure donnait un résultat impossible : le W échouait sur 11 % de
l'écran à 5000 et réussissait parfaitement à 10000 et 15000. La précision d'un
tampon de profondeur se dégrade avec la distance et ne s'améliore jamais.

L'anomalie ne frappait que le **tout premier cas mesuré**, ce qui désignait
l'état de la carte à l'ouverture plutôt que la profondeur. Deux images jetées
avant de mesurer l'ont fait disparaître, et les deux tampons se sont révélés
équivalents.

**Mesurer la première image après l'ouverture d'un contexte Glide, c'est mesurer
une machine qui n'a pas fini de s'installer.** Sans le doute qu'a levé
l'invraisemblance du profil — meilleur au loin qu'au près — la conclusion aurait
été « le Z est supérieur au W », consignée et fausse.

## L'accord avec la chaîne de transformation

La profondeur normalisée produite par la projection va de 0,50025 à 20 unités
jusqu'à 0,99983 à 15000. Sur toute cette plage, le proche masque le lointain.

C'est un point d'accord entre deux tickets, et un désaccord y produirait un tri
globalement faux — donc un décor passant devant un autre, défaut très visible
qu'on attribuerait au décodeur plutôt qu'à la plage.

## Les modes de rendu que DKR emploie

Relevés dans la source du portage voisin, par fréquence d'apparition :

| Famille | Occurrences | Ce que c'est | Traduction Glide |
|---|---|---|---|
| `FOG_SHADE_A` | 74 | brouillard sur alpha itérée | E05-S06 |
| `XLU_SURF` | 78 | translucide | `SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA` |
| `OPA_SURF` | 47 | opaque | `ONE` / `ZERO` |
| `TEX_EDGE` | 25 | découpe par seuil alpha | test alpha — la végétation |
| `DECAL` | 21 | surface collée, même profondeur | biais de profondeur |
| `INTER` | 12 | surfaces s'interpénétrant | biais de profondeur |
| `XLU_LINE_MOD` | 9 | lignes translucides | — |
| préfixe `AA_` | fréquent | anti-crénelage par couverture | **sans équivalent** |

Le préfixe `AA_` est le seul écart structurel. Le RDP fait un anti-crénelage par
couverture de pixel, intégré au mélangeur ; Glide 2 n'offre que
`grAADrawTriangle`, dont le coût est sans rapport. Ces modes sont donc rendus
sans anti-crénelage, ce qui est une dégradation visible sur les bords et **non
une erreur de couleur** — elle ne se cumule pas et ne se propage pas.

## Ce qui est déjà mesuré ailleurs

`glide_state_probe.c` a confirmé sur la carte, le même jour :

- mélange opaque, alpha et additif, à la quantification près ;
- test alpha, dans les deux sens — 0 pixel sous le seuil, 112000 au-dessus, soit
  exactement l'aire analytique du triangle ;
- fenêtre de ciseaux, bornes incluses à gauche et exclues à droite.

## Ce qui reste ouvert

- **`grChromakeyMode`** contre le test alpha : le ticket demande de comparer coût
  et résultat. Non mesuré.
- **Le biais de profondeur** pour les modes `DECAL` et `INTER` : `grDepthBiasLevel`
  existe, sa valeur utile se règle sur une scène réelle.
- **L'ordre de rendu des surfaces translucides** est vérifié structurellement —
  le décodeur émet dans l'ordre de la display list et ne regroupe rien — mais pas
  sur une scène du jeu.
- **Ombres, eau, particules et reflets** : demandent la ROM.
