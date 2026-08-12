# E00-S03 — Spike : coût CPU du code recompilé en 32 bits sans SSE

| | |
|---|---|
| **Épic** | E00 — Cadrage, mesures et décisions |
| **Statut** | IN_PROGRESS |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E00-S02 |
| **Bloque** | E00-S05, E08-S01, E08-S02 |

## État au 2026-08-11

Les **deux facteurs** du budget sont mesurés — le passage 64 → 32 bits sans SSE,
et la normalisation vers la machine cible réelle :
[`docs/research/cpu-budget.md`](../../research/cpu-budget.md).

| Grandeur | Mesure |
|---|---|
| Temps d'exécution 64 → 32 bits, facteur médian | **2,16×** (étendue 1,54× à 3,00×) |
| **Normalisation hôte 32 bits → Pentium II 400 MHz** | **17,7×** (étendue 15,6× à 21,7×) |
| **Facteur global, poste de développement → cible** | **≈ 38×** |
| Instructions x86 par instruction MIPS | 2,74 en 64 bits, **3,89** en 32 bits |
| Taille de code `.text` | **+24,8 %** |
| Instructions SSE dans le binaire 32 bits | **aucune**, vérifié au désassemblage |

Chaîne rendue opérationnelle sous Linux en chemin : toolchain MIPS sans droits
root, ELF du decomp **matching** (SHA-1 identique à la ROM du joueur),
`scripts/generate_recomp_toml.py`, et **587 625 lignes de C généré** par
N64Recomp. Banc rejouable : `tools/cpu-budget/run.sh`.

Un blocage franc trouvé et levé : `recomp.h` exigeait un entier 128 bits, absent
en 32 bits, ce qui arrêtait la compilation avant la première fonction. Il ne
servait qu'à `DMULT`/`DMULTU`, que **DKR n'appelle jamais** (zéro occurrence sur
3 823 fonctions). Corrigé par une implémentation portable prouvée équivalente sur
20 000 200 comparaisons — patch `patches/n64recomp/0002-…`.

**La normalisation vers la cible est faite** (étape 5). Le banc a été porté sur
Win32 et exécuté sur le Pentium II émulé de [E09-S01](../E09-qa/E09-S01-environnement-test-emule.md),
avec le même code généré, le même ELF et les mêmes entrées que sur l'hôte : seule
la machine change. 23 fonctions communes, dispersion étroite (15,6× à 21,7×), ce
qui indique une mesure saine.

Deux enseignements en marge : le rapport de fréquence hôte/cible étant d'environ
9 pour un facteur mesuré de 17,7, le Pentium II est environ **deux fois moins
efficace par cycle** sur ce code — plausible pour de la recompilation, faite de
longues chaînes de dépendances. Et le binaire de mesure, lié au **CRT complet de
mingw-w64**, démarre sans difficulté sous Windows 95 : c'est un résultat direct
pour [E00-S02](E00-S02-spike-toolchain-pe-win95.md), qui tenait cette question
pour l'inconnue principale.

**Le go/no-go n'est toujours pas prononcé**, mais ce qui manque a changé de
nature : ce n'est plus un facteur, c'est un **dénominateur**. Le banc mesure des
fonctions feuilles isolées ; il donne le coût relatif d'une machine à l'autre,
pas le coût absolu d'une image de jeu. Il faut désormais l'étape 2 (séquence de
jeu déterministe), qui exige [E02-S06](../E02-systeme/E02-S06-amorcage-jeu.md),
et le chiffre audio de [E00-S04](E00-S04-spike-cout-rsp-sans-sse.md).

Utilisable dès maintenant : le facteur **38×** transpose sur la cible toute
mesure faite sur le poste de développement.

## Contexte

C'est le ticket qui décide si ce projet est faisable.

La recompilation statique traduit chaque instruction MIPS en C. Sur un hôte
64 bits, c'est confortable : les 32 registres de 64 bits du VR4300 tiennent
naturellement dans les registres de l'hôte. Sur un Pentium II, chaque registre
du jeu devient une paire de mots de 32 bits, et chaque opération 64 bits une
séquence de plusieurs instructions x86. Le facteur multiplicatif n'est pas connu
et il n'est pas devinable.

À cela s'ajoute une charge que la console ne faisait pas porter au CPU : le
microcode audio, exécuté sur la N64 par un DSP vectoriel dédié à 62,5 MHz, tourne
ici sur le processeur hôte (E00-S04 le mesure séparément).

L'ordre de grandeur à battre : le VR4300 tourne à 93,75 MHz et le jeu vise
30 images par seconde. Un Pentium II à 400 MHz offre environ quatre fois le débit
d'instructions brut. Toute la question est de savoir ce que le surcoût de
traduction consomme de cette marge.

## Objectif

Mesurer le facteur de ralentissement du code recompilé compilé en 32 bits sans
SSE, et en déduire la fréquence CPU minimale requise. Livrer un **go / no-go**
chiffré, pas une impression.

## Périmètre

**Dans :** mesure du seul code CPU du jeu, hors rendu et hors audio.

**Hors :** optimisation (c'est E08-S02) ; toute la partie graphique.

## Travail

1. Construire le runtime existant en deux variantes sur le même hôte moderne :
   - référence : x86-64, options actuelles ;
   - cible : `-m32 -march=pentium2 -mfpmath=387 -mno-sse`, toolchain retenue par
     E00-S02.
2. Instrumenter une portion de jeu **déterministe et reproductible** : par
   exemple le démarrage jusqu'à l'écran-titre, puis une course fixée jouée par
   une trace d'entrées rejouée. Sans reproductibilité, la mesure ne compare rien.
3. Mesurer, avec le renderer `DiagnosticRenderer` (`null_renderer.cpp`) pour
   sortir le rendu de l'équation :
   - temps CPU total du fil de jeu par image, en médiane et au 99ᵉ centile ;
   - répartition par zone chaude (physique, IA, collisions, matrices).
4. En déduire le facteur cible / référence, puis, en normalisant par la fréquence
   de l'hôte de mesure, la fréquence minimale d'un Pentium II qui tient 33,3 ms
   par image en laissant une marge pour le rendu et l'audio.
5. Recouper avec une mesure indépendante : rejouer la même trace sur une machine
   32 bits réelle ou fortement bridée si l'atelier en dispose. Une extrapolation
   depuis un cœur moderne surestime toujours les vieilles machines — le rapport
   instructions par cycle, la taille des caches et la prédiction de branchement
   n'ont rien à voir.
6. Écrire `docs/research/cpu-budget.md` avec la méthode, les chiffres bruts, et
   la conclusion.

## Critères d'acceptation

- [ ] Le facteur de ralentissement 64 → 32 bits sans SSE est mesuré sur au moins
      deux séquences de jeu distinctes.
- [ ] Le budget par image est ventilé : CPU du jeu, microcode audio (chiffre
      repris de E00-S04), transformation des vertex, marge restante.
- [ ] La fréquence CPU minimale est énoncée en MHz, avec l'hypothèse de marge
      explicitée.
- [ ] Une conclusion **go / no-go** est écrite noir sur blanc, avec le seuil qui
      la déclencherait dans l'autre sens.
- [ ] Si le verdict est no-go sur Pentium II, le document indique ce qui
      changerait la donne : plancher matériel relevé (Pentium III), ou bascule
      vers le portage natif du decomp voisin.

## Risques

Le résultat peut condamner l'approche recomp sur cette classe de machine. C'est
une issue acceptable de ce ticket, et c'est même sa raison d'être : la découvrir
maintenant coûte une semaine, la découvrir après E04 et E05 en coûte trois mois.

Le repli existe et il est documenté : `/var/www/Diddy-Kong-Racing` porte un
backlog « Voodoo95 » de portage natif depuis le decomp, qui n'a pas ce surcoût de
traduction puisqu'il compile du C d'origine — au prix d'un travail bien plus
lourd sur le reste.

## Références

- `runtime-recomp/src/game/null_renderer.cpp` — renderer de diagnostic, idéal
  pour isoler le coût CPU
- `docs/ARCHITECTURE.md` — chemin d'exécution
- E00-S04 — coût du microcode audio, second terme du budget
