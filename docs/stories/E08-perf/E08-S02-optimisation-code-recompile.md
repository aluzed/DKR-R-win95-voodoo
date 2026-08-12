# E08-S02 — Optimisation du code recompilé

| | |
|---|---|
| **Épic** | E08 — Performance |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | L |
| **Dépend de** | E08-S01, E01-S05 |
| **Bloque** | — |

## Contexte

Le code recompilé est le plus gros poste du budget, et c'est aussi le plus
difficile à optimiser : il est généré, on ne l'édite pas, et il traduit fidèlement
les instructions du VR4300 y compris quand cette fidélité coûte cher.

Les leviers disponibles, du moins risqué au plus risqué :

1. **Les options du compilateur.** Le code généré est très répétitif ; le choix du
   niveau d'optimisation, de la stratégie d'inlining et de l'ordonnancement pour le
   Pentium II peut donner un gain notable pour un risque nul.
2. **La disposition du code.** Un binaire de plusieurs dizaines de mégaoctets sur
   une machine dont le cache d'instructions se compte en kilooctets : regrouper les
   fonctions chaudes améliore la localité, et c'est souvent là que se trouve le
   gain le plus important sur cette classe de machine.
3. **La politique de recompilation.** `dkr.us.v77.recomp-policy.json` pilote la
   génération. Certaines options de N64Recomp changent le code produit ; les
   examiner.
4. **Le remplacement de fonctions.** Le pipeline de crochets permet de remplacer
   une fonction du jeu par une implémentation native. Pour les rares fonctions
   très chaudes et purement calculatoires, c'est le levier le plus puissant — et
   celui qui met le plus en péril la fidélité, puisqu'il substitue du code écrit à
   la main à du code traduit.

L'ordre importe : les trois premiers leviers ne changent pas le comportement du
jeu, le quatrième si.

## Objectif

Réduire le coût du code recompilé, en préservant strictement le comportement du
jeu.

## Périmètre

**Dans :** options de compilation, disposition, politique de recompilation,
remplacement ciblé de fonctions.

**Hors :** le chemin graphique (E08-S03) et la mémoire (E08-S04).

## Travail

1. Identifier les fonctions chaudes à partir de l'export de E08-S01, sur une
   session de jeu réelle et non sur une boucle synthétique.
2. Explorer les options du compilateur, en mesurant chaque variante. Sur cette
   architecture, optimiser pour la taille peut battre optimiser pour la vitesse,
   parce que le cache est le facteur limitant — c'est contre-intuitif et cela se
   mesure.
3. Travailler la disposition du code : regrouper les fonctions chaudes. Si la
   toolchain retenue le permet, l'optimisation guidée par le profil est le moyen le
   plus direct d'y parvenir.
4. Examiner les options de la politique de recompilation et mesurer leur effet.
5. Pour les fonctions les plus chaudes, évaluer le remplacement natif. Le decomp
   (`extern/dkr-decomp`) fournit le C d'origine, ce qui rend l'exercice bien moins
   risqué qu'une réécriture : on compile la source d'origine plutôt que d'imiter
   son comportement. Attention toutefois — le portage natif voisin a découvert que
   ce C, sous `#ifdef NON_MATCHING`, **n'avait jamais été compilé par aucune
   cible** et portait cinq défauts francs. Il se vérifie, il ne se fait pas
   confiance.
6. Pour chaque remplacement, prouver l'équivalence par comparaison de sortie sur
   un large échantillon d'entrées, contre la version recompilée.
7. Mesurer le gain cumulé et le reporter au budget de E08-S01.
8. Vérifier la non-régression du jeu après chaque changement : une séance de jeu
   complète, pas seulement le démarrage.

## Critères d'acceptation

- [ ] Les fonctions chaudes sont identifiées sur une session de jeu réelle.
- [ ] Chaque option de compilation est mesurée, pas supposée.
- [ ] L'effet de la disposition du code est mesuré séparément.
- [ ] Tout remplacement natif est prouvé équivalent par comparaison de sortie sur
      un large échantillon.
- [ ] Le gain cumulé est mesuré et reporté au budget.
- [ ] Le jeu se comporte identiquement, vérifié par une séance complète.
- [ ] Aucun remplacement natif n'est fait sans mesure préalable prouvant que la
      fonction est chaude.

## Risques

Le remplacement de fonctions est le levier le plus tentant et le plus dangereux :
il substitue du code écrit à la main à un code traduit fidèlement, et une
différence de comportement peut ne se manifester que dans une situation de jeu
rare. Ne l'employer que sur des fonctions dont le profil prouve qu'elles comptent,
et jamais sans preuve d'équivalence.

## Références

- `runtime-recomp/dkr.us.v77.recomp-policy.json`
- `extern/dkr-decomp` — source C d'origine
- `../../Diddy-Kong-Racing/docs/stories/E01-build/E01-S04-porter-hasm-en-c.md` —
  cinq défauts trouvés dans le C `NON_MATCHING` du decomp
- E08-S01 — profil et budget
