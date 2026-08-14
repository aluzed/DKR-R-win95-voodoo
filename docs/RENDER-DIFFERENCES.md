# Ce qui ne ressemblera pas à la console, et pourquoi

Ce document liste les écarts **assumés** du portage par rapport au rendu de la
Nintendo 64. Il existe pour une raison précise : un écart documenté est une
caractéristique connue du portage ; le même écart non documenté sera signalé
comme un défaut à chaque comparaison, indéfiniment.

Chaque entrée dit ce qui diffère, pourquoi c'est irréductible, et ce que cela
donne à l'écran.

---

## Le filtrage de texture — trois points contre quatre

**Ce qui diffère.** Le RDP filtre sur un **triangle de trois texels** ; la
Voodoo 2 fait un bilinéaire classique sur **quatre**. Ce n'est pas un réglage,
c'est la structure de l'unité de filtrage.

**Pourquoi c'est irréductible.** Le filtrage à trois points n'est pas exprimable
avec les modes de Glide. Le reproduire demanderait de rendre chaque surface en
plusieurs passes avec des décalages de texture, à un coût sans rapport avec le
gain.

**Ce que cela donne.** L'écart est visible sur les textures de faible résolution
— c'est-à-dire sur la plupart de celles de DKR, la mémoire de texture de la
console étant très limitée. Le rendu du portage est légèrement plus lisse en
diagonale ; la console montre un motif triangulaire caractéristique sur les
dégradés étirés.

**Ce qui est offert en compensation.** Le mode point à point reste disponible, et
le jeu le demande déjà pour une partie de ses éléments — 30 entrées de table sur
214 emploient `G_TF_POINT`. Ces surfaces-là seront identiques à la console.

---

## L'anti-crénelage par couverture

**Ce qui diffère.** Le RDP calcule une couverture de pixel et la fait intervenir
dans le mélangeur — c'est le préfixe `AA_` des modes de rendu, très présent dans
DKR. Glide 2 n'offre que `grAADrawTriangle`, dont le coût est sans rapport avec
celui d'un triangle ordinaire.

**Pourquoi c'est irréductible.** Sur une machine de 1998 dont le remplissage est
la ressource limitante, dessiner chaque bord une seconde fois n'est pas
envisageable.

**Ce que cela donne.** Les bords de polygone sont francs là où la console les
adoucit. C'est une dégradation **de bord**, pas de couleur : elle ne se cumule
pas d'une surface à l'autre et ne se propage pas dans l'image.

---

## Le combineur de couleurs — quatorze configurations sur vingt-neuf

**Ce qui diffère.** Le combineur du RDP est programmable ; celui de Glide est
fixe. Sur les vingt-neuf configurations que DKR emploie, douze se traduisent
exactement, dix demandent plusieurs passes, quatre sont approchées et trois
relèvent de la seconde unité de texture.

**Pourquoi c'est irréductible.** Deux limites structurelles, mesurées et non
supposées : Glide n'a **qu'un registre de couleur constante** là où le RDP en a
deux, et **aucun de ses seize facteurs ne délivre l'alpha de ce registre**.

**Ce que cela donne.** Les écarts mesurés vont de 99 à 148 unités sur 255 pour
les configurations concernées. C'est significatif, et c'est l'écart le plus
important de cette liste. Le détail par configuration est dans
`research/combiner-mapping.md`.

**Ce qui reste à tenter.** Faire voyager la seconde constante dans l'alpha du
sommet. L'issue est restreinte par le brouillard, qui occupe déjà cette place sur
74 modes de rendu, mais elle reste ouverte pour les surfaces sans brouillard.

---

## Les textures lointaines scintillent

**Ce qui diffère.** Rien : DKR n'emploie pas de mipmaps — `G_TL_TILE` quinze fois
dans la source, `G_TL_LOD` aucune.

**Ce que cela donne.** Les textures vues de loin scintillent **exactement comme
sur la console**. Ce n'est donc pas un écart, et l'entrée figure ici parce que
c'est le genre de chose qu'on prend pour un défaut du portage.

Ajouter des mipmaps que le jeu ne demande pas serait un écart, pas une
correction : cela coûterait un tiers de la mémoire de texture et changerait
l'apparence.

---

## La quantification des couleurs

**Ce qui diffère.** Le tampon d'image de la Voodoo 2 est en 565 — cinq bits de
rouge et de bleu, six de vert. Le combineur multiplie en 0..255 et **tronque**,
ce qui coûte un pas de quantification supplémentaire.

**Ce que cela donne.** Un écart maximal de 9 unités sur 255, mesuré sur la
comparaison complète avec le rastériseur de référence. Invisible sur une surface,
perceptible en bande sur un dégradé très étiré.

---

## Le tri en profondeur

**Ce qui diffère.** Seize bits, contre le format compressé de la N64. Mesuré : le
tampon sépare deux surfaces distantes de deux pour mille à toute distance de la
plage de jeu, et cède entièrement à deux dixièmes de pour mille.

**Ce que cela donne.** Rien de visible dans les conditions du jeu, sauf sur des
surfaces plus rapprochées que ce que la piste présente. Le mur est connu et
mesuré, ce qui permettra de reconnaître le symptôme s'il apparaît.
