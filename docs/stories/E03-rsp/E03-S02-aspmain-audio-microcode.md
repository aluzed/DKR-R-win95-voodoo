# E03-S02 — Microcode audio `aspMain` : exécution et budget

| | |
|---|---|
| **Épic** | E03 — RSP sur x86 sans SSE |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E03-S01, E02-S02 |
| **Bloque** | E06-S03, E03-S03 |

## Contexte

`aspMain` est le microcode audio de Rare, recompilé instruction par instruction
en 73 Ko de C++. Il produit les tampons audio du jeu à partir des commandes
émises par le moteur.

Sa configuration de recompilation (`runtime-recomp/rsp/aspMain.us.v77.toml`) est
instructive sur sa nature : le microcode est chargé à l'adresse IMEM `0x04001080`
plutôt qu'à `0x1000` comme les révisions ultérieures, et seize cibles de
branchement indirect ont dû être déclarées à la main parce que le répartiteur
d'ABI les lit dans une table de commandes, hors de portée de l'analyse statique.
Ce n'est pas du microcode standard, et il ne se remplace pas par une
implémentation générique.

Une fois E03-S01 livré, `aspMain` doit s'exécuter correctement sur la cible. Reste
à vérifier qu'il produit le bon son, et à quel prix.

## Objectif

Faire produire à `aspMain` des tampons audio corrects sous Windows 95, et
mesurer sa part exacte du budget CPU.

## Périmètre

**Dans :** l'exécution du microcode recompilé, sa validation, sa mesure.

**Hors :** l'émulation vectorielle (E03-S01) et la restitution sonore (E06-S03).

## Travail

1. Exécuter `dkrAspMain` sur la cible avec l'implémentation de E03-S01, sur des
   tâches audio réelles produites par le jeu.
2. Comparer les tampons produits à ceux de la cible moderne, au bit près. Toute
   différence remonte à E03-S01, pas à un réglage de mixage.
3. Vérifier l'ordonnancement : la tâche audio est soumise par le jeu au même titre
   que la tâche graphique, et son achèvement doit être signalé au fil demandeur.
   Plusieurs patchs existants portent sur cette mécanique de complétion
   (`0011-acknowledge-sp-delivery-before-dp`, `0012-wait-for-emulated-sp-handler`) :
   vérifier qu'ils restent corrects avec la couche de E02-S01.
4. Mesurer le temps d'exécution par tâche audio sur la cible, en médiane et au
   99ᵉ centile. Le centile haut compte davantage que la médiane : c'est lui qui
   provoque les coupures sonores.
5. Convertir en pourcentage du budget par image et l'inscrire au budget global de
   E08-S01.
6. Identifier les zones chaudes du microcode et vérifier qu'elles correspondent à
   la distribution prévue par E00-S04. Un écart signale une hypothèse fausse dans
   le spike, qu'il vaut mieux corriger que traîner.
7. Statuer sur le déclenchement de E03-S03 : si le budget est dépassé et que
   l'optimisation ne suffit pas, le mixeur de haut niveau devient nécessaire.

## Critères d'acceptation

- [ ] `dkrAspMain` s'exécute sur la cible et produit des tampons identiques au bit
      près à ceux de la cible moderne.
- [ ] La complétion de tâche est signalée correctement, chemins d'arrêt inclus.
- [ ] Le temps par tâche est mesuré en médiane et au 99ᵉ centile.
- [ ] La part du budget par image est chiffrée et inscrite au budget global.
- [ ] Les zones chaudes sont identifiées et comparées à la prévision de E00-S04.
- [ ] La décision de déclencher ou non E03-S03 est prise sur ce chiffre.

## Risques

L'audio est impitoyable sur le respect des échéances : une image de rendu en
retard produit une saccade que l'œil pardonne, un tampon audio en retard produit
un craquement que l'oreille ne pardonne pas. Le budget audio doit donc être tenu
au centile haut, pas en moyenne.

## Références

- `runtime-recomp/RecompiledRSP/aspMain.cpp`
- `runtime-recomp/rsp/aspMain.us.v77.toml`
- `patches/n64-modern-runtime/0011-acknowledge-sp-delivery-before-dp.patch`,
  `0012-wait-for-emulated-sp-handler.patch`
