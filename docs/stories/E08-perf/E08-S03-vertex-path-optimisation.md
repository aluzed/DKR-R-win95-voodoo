# E08-S03 — Optimisation du chemin sommet

| | |
|---|---|
| **Épic** | E08 — Performance |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | L |
| **Dépend de** | E08-S01, E04-S03, E04-S05 |
| **Bloque** | — |

## Contexte

La transformation des sommets est un travail que la N64 confiait au RSP et qui
revient ici au processeur hôte. C'est du calcul régulier, en volume, sur des
données contiguës — exactement le profil qui se prête à l'optimisation.

Trois leviers, dans l'ordre de rentabilité habituelle :

1. **Ne pas calculer.** Le rejet précoce par volume englobant (E04-S05) est le
   gain le moins cher : un objet rejeté est un objet dont aucun sommet n'est
   transformé. C'est presque toujours le levier le plus rentable, et il est
   souvent négligé au profit du suivant.
2. **Calculer mieux.** Le choix entre x87 et virgule fixe, tranché en E04-S03,
   peut être réexaminé à la lumière du profil réel. Le x87 du Pentium II a une
   latence notable et une pile de registres contrainte ; MMX offre des entiers
   16 bits en parallèle, ce qui convient à la virgule fixe.
3. **Calculer moins souvent.** La géométrie statique d'un niveau est transformée à
   chaque image alors que seule la matrice de vue change ; il peut y avoir des
   invariants à exploiter, à condition de vérifier qu'ils tiennent réellement.

Un point de vigilance : MMX partage ses registres avec la pile x87 (voir E03-S01),
et mélanger les deux dans le pipeline sommet impose des transitions coûteuses. Si
MMX est retenu, il doit couvrir un bloc entier, pas quelques opérations isolées.

## Objectif

Ramener le chemin sommet dans son allocation de budget, mesure à l'appui.

## Périmètre

**Dans :** transformation, découpage, préparation des sommets pour Glide.

**Hors :** le code recompilé (E08-S02) et le backend Glide côté carte.

## Travail

1. Établir le profil détaillé du chemin sommet à partir de E08-S01 : part de la
   transformation, du découpage, du rejet, de la préparation.
2. Travailler d'abord le rejet. Mesurer combien de sommets sont transformés pour
   rien — c'est-à-dire appartenant à de la géométrie finalement invisible. Si ce
   chiffre est élevé, tout le reste du ticket est secondaire.
3. Optimiser la boucle de transformation : disposition des données en mémoire,
   déroulage, préchargement si l'architecture le permet. Sur un Pentium II, la
   disposition des données pèse souvent plus que le nombre d'instructions.
4. Évaluer MMX pour la virgule fixe, en couvrant un bloc entier du pipeline pour
   éviter les transitions avec x87. Mesurer avant d'écrire beaucoup de code.
5. Éliminer les recopies. Le vertex doit être produit directement au format Glide
   (E04-S01) ; vérifier qu'aucune conversion intermédiaire ne subsiste, et que le
   tampon de sommets est réutilisé plutôt que réalloué.
6. Explorer le dessin par tableaux de sommets plutôt que triangle par triangle, si
   la version de Glide retenue le permet : cela réduit le nombre d'appels, dont le
   coût unitaire n'est pas négligeable.
7. Mesurer le gain à chaque étape et l'inscrire au budget. Une optimisation dont
   le gain n'est pas mesuré est une complication.
8. Vérifier la non-régression visuelle après chaque changement, par comparaison
   d'images (E09-S02). Une optimisation de calcul géométrique qui change une
   position d'un pixel doit se voir.

## Critères d'acceptation

- [ ] Le profil détaillé du chemin sommet est établi.
- [ ] Le nombre de sommets transformés inutilement est mesuré, et le rejet
      travaillé en premier.
- [ ] Chaque optimisation est mesurée séparément.
- [ ] MMX n'est employé que si la mesure le justifie, et sur des blocs entiers.
- [ ] Aucune recopie ni réallocation par image dans le chemin sommet.
- [ ] Le dessin par tableaux de sommets est évalué.
- [ ] Le chemin sommet tient dans son allocation de budget.
- [ ] Aucune régression visuelle après optimisation, vérifiée par comparaison
      d'images.

## Risques

L'optimisation de code géométrique introduit facilement des écarts de précision.
Un changement d'ordre d'opérations en flottant, un arrondi différent en virgule
fixe, et la géométrie se met à trembler. La vérification par comparaison d'images
après chaque étape n'est pas une précaution excessive : c'est ce qui permet
d'attribuer une régression à l'optimisation qui l'a causée, plutôt qu'à
l'ensemble.

## Références

- E04-S03 — transformation, choix flottant / virgule fixe
- E04-S05 — découpage et rejet
- E03-S01 — MMX et transitions avec x87
- E08-S01 — profil et budget
