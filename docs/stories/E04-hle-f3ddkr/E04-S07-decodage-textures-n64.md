# E04-S07 — Décodage des textures N64

| | |
|---|---|
| **Épic** | E04 — HLE F3DDKR indépendant de RT64 |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E04-S02, E04-S06 |
| **Bloque** | E05-S02, E05-S08 |

## Contexte

La N64 et la Voodoo ne stockent pas les textures de la même façon, et l'écart
porte sur trois points à la fois.

**Les formats.** La N64 propose RGBA16, RGBA32, IA16, IA8, IA4, I8, I4, CI8 et
CI4. La Voodoo propose RGB565, ARGB1555, ARGB4444, intensité, intensité-alpha, et
un format palettisé 8 bits. La correspondance est bonne pour certains, imparfaite
pour d'autres : RGBA16 de la N64 est un ARGB1555, transposition directe ; I4 et
IA4 n'ont pas d'équivalent et demandent une expansion, donc un doublement de
l'occupation mémoire.

**Les palettes.** Les textures CI4 et CI8 sont indexées, et la N64 dispose d'une
mémoire de palette dédiée. La Voodoo n'a **qu'une seule palette active à la fois
par TMU** : deux textures palettisées de palettes différentes ne peuvent pas être
liées simultanément. C'est une contrainte structurante, et la solution la plus
sûre est souvent de développer les textures indexées vers un format direct — au
prix de la mémoire de texture, qui est justement la ressource rare.

**L'entrelacement.** La N64 stocke ses textures dans un ordre entrelacé par
lignes impaires. Le décodage doit le défaire.

S'ajoute la contrainte de format de la carte : dimensions en puissance de deux,
256 × 256 au maximum, rapport d'aspect borné.

## Objectif

Décoder toutes les textures utilisées par DKR vers des formats acceptés par la
carte cible, avec un coût et une occupation mémoire mesurés.

## Périmètre

**Dans :** décodage, conversion de format, palettes, cache côté hôte.

**Hors :** le placement en mémoire de texture (E05-S02) et le filtrage (E05-S08).

## Travail

1. Inventorier les formats et les tailles de texture réellement utilisés par DKR,
   avec leur fréquence. Le portage natif voisin a extrait **2 687 textures** de la
   ROM et dispose déjà de cette information.
2. Écrire le décodeur pour chaque format N64 vers un format cible, en documentant
   la perte éventuelle. RGBA32 en particulier ne survit pas tel quel : la Voodoo
   est une carte 16 bits, et la conversion doit être choisie — tramage, ou
   troncature.
3. Défaire l'entrelacement N64. Le tester sur des textures de tailles diverses :
   c'est un bug classique qui ne se manifeste qu'à certaines largeurs.
4. Trancher le traitement des textures indexées : palette matérielle unique, ou
   développement vers un format direct. Décider **sur une mesure** de l'occupation
   mémoire résultante, confrontée au budget de mémoire de texture par TMU de
   E00-S05.
5. Traiter les dimensions non conformes : mise à l'échelle vers une puissance de
   deux, ou remplissage. Vérifier que les coordonnées de texture sont ajustées en
   conséquence — c'est l'endroit où l'on introduit des décalages d'un demi-texel
   qui se voient sur les bords.
6. Implémenter un cache côté hôte indexé par la clé de la texture, pour éviter de
   redécoder à chaque image. Dimensionner le cache sur le budget de E00-S06.
7. Mesurer le coût du décodage : par texture et par image, en régime établi et
   lors d'un chargement de niveau. Un décodage coûteux au chargement est
   acceptable ; en cours de course, il ne l'est pas.
8. Comparer les textures décodées à celles de la cible moderne, en tenant compte
   de la réduction de profondeur de couleur.

## Critères d'acceptation

- [ ] Tous les formats de texture utilisés par DKR sont décodés.
- [ ] L'entrelacement est correctement défait, testé sur plusieurs largeurs.
- [ ] Le traitement des textures indexées est tranché sur une mesure d'occupation
      mémoire, confrontée au budget par TMU.
- [ ] Les dimensions non conformes sont traitées sans décalage de texel visible.
- [ ] Le cache évite le redécodage en régime établi, et son occupation respecte le
      budget.
- [ ] Le coût de décodage est mesuré, au chargement et en cours de partie.
- [ ] Les textures décodées sont comparées à la référence moderne, l'écart étant
      attribuable à la seule réduction de profondeur de couleur.

## Risques

Le développement des textures indexées vers un format direct peut faire exploser
l'occupation de mémoire de texture : une CI4 développée en ARGB1555 occupe quatre
fois plus. Si le pic par niveau dépasse la mémoire de la TMU, il faudra revenir à
la palette matérielle et gérer ses changements — ce qui impose de regrouper les
dessins par palette, donc de contraindre l'ordre de rendu. Cette dépendance doit
être évaluée en E05-S02, pas découverte à l'exécution.

## Références

- `../../Diddy-Kong-Racing/docs/research/level-working-set.md` — pic de 1,20 Mo
  par niveau
- `../../Diddy-Kong-Racing/docs/stories/E02-assets/E02-S03-conversion-textures-palettes.md`
- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — `SetTextureImage`, `LoadBlock`
- E00-S05 — mémoire de texture par TMU
