# E04-S05 — Découpage, viewport et fenêtre de ciseaux

| | |
|---|---|
| **Épic** | E04 — HLE F3DDKR indépendant de RT64 |
| **Statut** | IN_PROGRESS |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E04-S03 |
| **Bloque** | E05-S01, E04-S08 |

## Contexte

Les cartes 3dfx ne découpent pas la géométrie. Elles disposent d'une fenêtre de
ciseaux (`grClipWindow`) qui rejette les fragments hors zone, mais un triangle
dont un sommet passe **derrière la caméra** ne peut pas être rejeté au niveau du
fragment : sa projection est mathématiquement absurde, et il faut le découper
avant projection, dans l'espace homogène.

C'est donc au CPU d'assurer :

- le rejet des triangles entièrement hors du volume de vue — bon marché et
  rentable, car chaque triangle rejeté est un triangle non transformé et non
  envoyé ;
- le découpage effectif des triangles qui traversent le plan proche, qui produit
  de nouveaux sommets et parfois plusieurs triangles pour un ;
- l'élimination des faces arrière, que la carte ne fait pas non plus.

Un point favorable : Glide tolère des coordonnées écran modérément hors de
l'écran, et son unité de ciseaux fait le reste. Seul le plan proche exige un vrai
découpage. Cette distinction est la clé du coût : le découpage complet aux six
plans serait très cher et n'est pas nécessaire.

## Objectif

Livrer un étage de découpage correct et bon marché, qui ne remet au backend que
des primitives que la carte peut rastériser.

## Périmètre

**Dans :** rejet, découpage au plan proche, élimination des faces arrière,
viewport, fenêtre de ciseaux.

**Hors :** la configuration Glide de la fenêtre de ciseaux (E05-S01).

## Travail

1. Implémenter le rejet par volume englobant, en amont de la transformation quand
   c'est possible. Un objet entier rejeté avant transformation économise tous ses
   sommets.
2. Implémenter le découpage au plan proche dans l'espace homogène, avec
   interpolation de tous les attributs du sommet : couleur, coordonnées de
   texture, brouillard. Oublier un attribut produit un artefact visible uniquement
   sur les triangles découpés, donc rare et déroutant.
3. Vérifier expérimentalement la marge tolérée par Glide en dehors de l'écran, et
   caler dessus la décision « découper ou laisser passer ». Cette marge se mesure,
   elle ne se déduit pas de la documentation.
4. Implémenter l'élimination des faces arrière selon la convention du microcode.
   Relever cette convention dans le decomp : le sens d'orientation retenu par la
   N64 et l'état qui l'active.
5. Implémenter le viewport à partir de la commande du microcode : échelle et
   translation, en cohérence avec la transformation de E04-S03.
6. Implémenter la fenêtre de ciseaux et la traduire vers `grClipWindow`. DKR s'en
   sert notamment pour l'affichage en écran partagé multijoueur — cas à tester
   explicitement, y compris à quatre joueurs.
7. Mesurer la part du budget consommée par cet étage, et le nombre de triangles
   effectivement découpés sur une scène type. Si ce nombre est faible, l'étage
   n'a pas à être optimisé ; s'il est élevé, il devient un candidat pour E08-S03.

## Critères d'acceptation

- [x] Les triangles traversant le plan proche sont découpés, tous attributs
      interpolés — position, couleur **et** coordonnées de texture, chacun
      vérifié séparément. L'oubli d'un seul ne se verrait que sur les triangles
      découpés, donc rarement ; l'auto-test le confirme en le provoquant.
      Le cas à un sommet derrière produit bien **deux** triangles : le polygone
      restant est un quadrilatère, et ne pas le retrianguler ferait disparaître
      la moitié de la surface.
- [~] La marge de tolérance de Glide hors écran est mesurée et exploitée — **la
      marge de Glide n'est toujours pas mesurée** : elle demande de lire le
      tampon d'image de la carte (`grLfbLock`), la sortie d'une Voodoo
      passthrough n'apparaissant dans aucune capture de l'émulateur.
      **Mais une contrainte différente, elle, est mesurée et exploitée** : la
      précision. Un sommet créé au plan proche projetait à 16 millions de pixels,
      où les fonctions d'arête perdent tout sens. Le découpeur borne désormais
      les coordonnées par une **bande de garde** à quatre demi-écrans, ce qui a
      fait passer l'écart entre l'hôte et la cible de 2,59 % à 0,62 % des pixels.
      Les deux marges répondent à des questions distinctes et la seconde
      n'attend pas la première.
- [x] L'élimination des faces arrière suit la convention du microcode : le sens
      dépend du **signe de l'échelle en x de la fenêtre**, relevé dans
      `f3ddkr_rt64.cpp`. Une fenêtre miroir inverse l'orientation apparente, et
      éliminer le mauvais côté viderait l'écran.
- [ ] Le viewport correspond à celui de la cible moderne — **bloqué**, la
      comparaison demandant des scènes capturées.
- [~] L'écran partagé est correct à deux, trois et quatre joueurs — les quatre
      dispositions sont calculées et vérifiées, y compris **l'absence de
      chevauchement** entre quadrants voisins. Mais c'est de la géométrie de
      rectangles : le jeu ne l'a pas encore exercée.
- [ ] La part du budget et le nombre de triangles découpés sont mesurés —
      **bloqué** : le nombre découpé dépend d'une scène réelle, et sans lui la
      part du budget n'a pas de sens.
- [~] Aucune primitive ne parvient au backend hors domaine — le rejet hors écran
      existe et n'écarte un triangle que si **les trois** sommets sont du même
      côté. **Pas d'assertion en build de développement** : il n'y a pas encore
      de chemin complet du décodeur au backend où la poser.

## Risques

Le découpage est un classique des erreurs subtiles : un triangle mal découpé
produit un éclat de géométrie qui traverse l'écran, phénomène très visible et
difficile à reproduire, parce qu'il dépend d'un angle de caméra précis. Les tests
doivent inclure une caméra qui traverse la géométrie, pas seulement une caméra qui
la regarde.

## Références

- `runtime-recomp/src/game/f3ddkr_rt64.cpp` — commandes de viewport et de ciseaux
- E04-S03 — transformation, en amont
- E05-S01 — configuration Glide, en aval
