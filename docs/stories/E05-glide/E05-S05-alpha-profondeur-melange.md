# E05-S05 — Alpha, tampon de profondeur et mélange

| | |
|---|---|
| **Épic** | E05 — Backend Glide |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E04-S06, E05-S01 |
| **Bloque** | E09-S02 |

## Contexte

Le RDP dispose d'un mélangeur configurable, d'un test de profondeur, et de modes
de rendu qui combinent les deux de façon parfois inattendue. Glide offre
`grAlphaBlendFunction`, `grAlphaTestFunction`, `grDepthBufferFunction` et
`grDepthMask` — proches dans l'esprit, avec des écarts qui comptent :

- **La profondeur.** Le tampon de profondeur de Glide est en 16 bits, et la carte
  propose au choix un tampon en Z ou en W. Le W offre une bien meilleure
  répartition de la précision en profondeur, ce qui compte sur un jeu de course où
  la piste s'étend loin devant. La N64 avait sa propre gestion, avec son propre
  format compressé ; la correspondance n'est pas directe et doit être choisie.
- **Le test alpha.** DKR l'utilise massivement pour la végétation et les
  billboards découpés. Glide propose un test alpha classique, et
  `grChromakeyMode` comme alternative — à évaluer, le second pouvant être moins
  coûteux selon la configuration.
- **Le mélange.** Le mélangeur du RDP est plus expressif que celui de Glide,
  notamment parce qu'il peut faire intervenir la profondeur. Certaines
  configurations n'auront pas d'équivalent exact.

## Objectif

Réaliser les modes de profondeur, de test alpha et de mélange utilisés par DKR,
avec les artefacts de précision maîtrisés.

## Périmètre

**Dans :** test et écriture de profondeur, test alpha, mélange, ordre de rendu des
surfaces translucides.

**Hors :** le combineur (E05-S03) et le brouillard (E05-S06).

## Travail

1. Relever, à partir de l'inventaire de E04-S06, les modes de rendu réellement
   utilisés par le jeu, avec leur fréquence.
2. Trancher entre tampon en Z et tampon en W, sur une mesure des artefacts de
   précision : chercher le combat de profondeur sur des surfaces coplanaires
   éloignées, cas fréquent sur une piste de course. Consigner la comparaison.
3. Régler la plage de profondeur en cohérence avec la valeur produite par
   E04-S03. C'est un point d'accord entre deux tickets, et un désaccord y produit
   un tri en profondeur globalement faux — donc très visible.
4. Implémenter le test alpha, et comparer avec `grChromakeyMode` sur le coût et
   sur le résultat. Vérifier le rendu de la végétation, qui en dépend directement.
5. Implémenter les modes de mélange, en signalant ceux qui n'ont pas d'équivalent
   exact et en mesurant leur écart au rastériseur de référence (E04-S08).
6. Traiter l'ordre de rendu des surfaces translucides. La N64 dessinait dans
   l'ordre de la display list, et le jeu en dépend : reproduire cet ordre plutôt
   que de trier. Vérifier qu'aucune optimisation par lot n'a réordonné les
   primitives translucides — c'est un piège classique de tout regroupement d'état.
7. Vérifier les cas connus pour révéler ces défauts : ombres des véhicules, eau,
   effets de particules, reflets.
8. Mesurer le coût du test de profondeur et du mélange dans le budget de
   remplissage.

## Critères d'acceptation

- [ ] Les modes de rendu utilisés sont relevés et implémentés.
- [ ] Le choix Z / W est justifié par une mesure d'artefacts de précision
      consignée.
- [ ] La plage de profondeur est cohérente avec E04-S03, vérifiée sur une scène
      profonde.
- [ ] Le test alpha rend correctement la végétation, et le choix test alpha /
      chroma-key est justifié.
- [ ] Les modes de mélange sans équivalent exact sont identifiés et leur écart
      mesuré.
- [ ] L'ordre de rendu des surfaces translucides suit celui de la display list.
- [ ] Ombres, eau, particules et reflets sont vérifiés visuellement contre la
      référence.

## Risques

Le combat de profondeur en 16 bits est le risque principal, et il ne se manifeste
pas sur une scène de test : il se manifeste au loin, sur une piste longue, en
mouvement. Il faut le chercher activement, dans les conditions où il apparaît,
plutôt que d'attendre qu'il se signale.

## Références

- E04-S06 — inventaire des modes de rendu
- E04-S08 — oracle de comparaison
- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — traitement actuel des ombres
