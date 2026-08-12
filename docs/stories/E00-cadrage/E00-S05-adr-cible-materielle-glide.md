# E00-S05 — ADR : cible matérielle et version de Glide

| | |
|---|---|
| **Épic** | E00 — Cadrage, mesures et décisions |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | S |
| **Dépend de** | E00-S03, E00-S04 |
| **Bloque** | E05-S01, E05-S02, E05-S04, E09-S01, E09-S04 |

## Contexte

« Compatible 3dfx Voodoo » désigne cinq générations de cartes aux capacités très
différentes, et deux API incompatibles entre elles. Le choix conditionne
directement le travail de rendu — en particulier le nombre de TMU disponibles,
qui décide si les configurations de combiner à deux texels passent en une passe
ou en deux.

| Carte | TMU | Mémoire de texture | API |
|---|---|---|---|
| Voodoo Graphics (Voodoo 1) | 1 | 2 Mo | Glide 2.x |
| Voodoo 2, 8 Mo | 2 | 2 × 2 Mo | Glide 2.4 · Glide 3.x |
| Voodoo 2, 12 Mo | 2 | 2 × 4 Mo | Glide 2.4 · Glide 3.x |
| Voodoo Banshee | 1 | partagée | Glide 3.x |
| Voodoo 3 | 2 | 16 Mo | Glide 3.x |

Contrainte commune à toute la gamme : **aucune transformation matérielle**. La
carte reçoit des sommets déjà projetés en coordonnées écran. Tout le pipeline
géométrique — matrices, transformation, éclairage, découpage — reste à la charge
du CPU, ce qui pèse sur le budget mesuré par E00-S03. Les textures sont en
puissance de deux, 256 × 256 au maximum sur Voodoo 1 et 2.

Le cadrage projet a retenu la classe **Pentium II / III, Voodoo 2 ou 3, 64 Mo de
RAM, Windows 95 OSR2.5**. Cette ADR l'acte formellement et en tire les
conséquences chiffrées.

## Objectif

Écrire `docs/adr/0002-cible-materielle.md` : plancher matériel, configuration
recommandée, version de Glide, résolution de référence — chaque valeur justifiée
par une mesure ou une contrainte matérielle, pas par une préférence.

## Périmètre

**Dans :** la décision et sa justification.

**Hors :** l'implémentation Glide (E05).

## Travail

1. Reprendre les conclusions de E00-S03 et E00-S04 pour fixer le plancher CPU en
   MHz. Si le no-go de E00-S03 est tombé, cette ADR acte la sortie du projet ou
   le relèvement du plancher — elle ne contourne pas le chiffre.
2. Trancher **Glide 2.4 contre Glide 3.x**. Glide 3.x couvre Voodoo 2, Banshee et
   Voodoo 3 par une seule API et simplifie le support ; Glide 2.4 reste la seule
   voie vers Voodoo 1. Vérifier dans les sources 3dfx ouvertes quelles cibles
   chaque arbre construit réellement, plutôt que de se fier à la documentation
   commerciale d'époque.
3. Fixer le nombre de TMU **exigé** et le nombre **exploité**. Deux TMU
   permettent une passe unique pour les combiners à deux texels ; le rendu doit
   néanmoins rester correct sur une seule TMU, par repli multipasse (E05-S04).
4. Fixer la résolution de référence. 640 × 480 en 16 bits est le point d'équilibre
   d'une Voodoo 2 ; vérifier que le tampon d'image et le tampon de profondeur y
   tiennent dans la mémoire de la carte visée, en incluant le triple buffering
   s'il est retenu.
5. Établir le budget de mémoire de texture par niveau et le confronter à la
   mémoire par TMU. Le portage natif voisin a déjà mesuré un pic de **1,20 Mo**
   par niveau (`../../Diddy-Kong-Racing/docs/research/level-working-set.md`) —
   chiffre à réutiliser, en vérifiant qu'il porte bien sur le même jeu de niveaux.
6. Nommer la configuration de validation de E09-S04 : le matériel réel sur lequel
   la version sera déclarée bonne.

## Critères d'acceptation

- [ ] `docs/adr/0002-cible-materielle.md` fixe : CPU plancher, CPU recommandé,
      RAM, carte 3dfx plancher, carte recommandée, version de Glide, résolution.
- [ ] Chaque valeur renvoie à la mesure ou à la contrainte matérielle qui la
      justifie.
- [ ] Le choix Glide 2.4 / 3.x est argumenté sur la couverture matérielle réelle
      des sources 3dfx, pas sur la documentation d'époque.
- [ ] Le budget de mémoire de texture par TMU est chiffré et comparé au pic
      mesuré par niveau.
- [ ] La configuration de validation matériel réel est nommée.
- [ ] L'ADR indique ce qui la rouvrirait — par exemple un dépassement de budget
      de texture découvert en E05-S02.

## Vérifié en amont par E09-S01

La machine de test est montée et une démonstration Glide y tourne
([E09-S01](../E09-qa/E09-S01-environnement-test-emule.md)). Trois faits en
sortent, à reprendre ici :

- **Glide 2.54 et Glide 3.01 sont tous deux fournis** par le pilote de référence
  3dfx pour Voodoo 2. Le choix d'API de l'étape 2 ne dépend donc pas du matériel :
  `glide2x.dll` et `glide3x.dll` cohabitent sur la même machine.
- **Le modèle de carte émulé doit être vérifié dans le dialogue de réglages de
  86Box, pas déduit du fichier de configuration.** Les réglages Voodoo écrits à
  la main y ont été ignorés en silence : la machine a émulé une Voodoo 1 à 2 Mo
  alors que le fichier annonçait une Voodoo 2 à 4 Mo. Toute mesure de budget de
  texture ou de multitexture faite sans cette vérification serait fausse.
- **L'identifiant PCI ne suffit pas à identifier la carte** sur cette
  plate-forme. La détection à l'exécution de E05-S01 doit passer par
  `grSstQueryHardware` / `grSstQueryBoards`.

## Risques

Élargir la cible coûte cher et se paie en E05 : une TMU contre deux, c'est une
seconde passe de rendu sur une partie du jeu, donc un budget de remplissage
doublé sur ces surfaces. Mieux vaut un plancher étroit et tenu qu'une compatibilité
large et fausse.

## Références

- `../../Diddy-Kong-Racing/docs/research/level-working-set.md` — pic de working set
  par niveau, déjà mesuré côté portage natif
- [Sources Glide 3dfx](https://sourceforge.net/projects/glide/) ·
  [sezero/glide](https://github.com/sezero/glide) ·
  [hatarch/glide3x](https://github.com/hatarch/glide3x)
