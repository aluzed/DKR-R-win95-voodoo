# E01-S06 — Génération des sources hors de Windows

| | |
|---|---|
| **Épic** | E01 — Chaîne de build 32 bits Windows 95 |
| **Statut** | TODO |
| **Priorité** | P2 |
| **Estimation** | M |
| **Dépend de** | — |
| **Bloque** | E09-S03 |

## Contexte

Aujourd'hui, la préparation des sources générées est un chemin Windows exclusif.
`Build-Linux.sh` le dit sans détour :

> `Generated DKR functions are missing. Prepare them on Windows with
> Build-DKR-Runtime.cmd first.`

Et `docs/BUILDING.md` exige Visual Studio 2022, PowerShell **et** WSL2 pour cette
étape. Autrement dit : construire le decomp de référence, appliquer les patchs,
lancer N64Recomp et RSPRecomp demandent une machine Windows complète — alors que
la cible de ce projet se construit par cross-compilation depuis Linux (E01-S01).

Le poste de développement de ce portage est sous Linux. Devoir passer par une
machine Windows à chaque changement de politique de recompilation transforme une
boucle de quelques minutes en une boucle de plusieurs dizaines.

## Objectif

Rendre la génération complète — decomp de référence, patchs, N64Recomp, RSPRecomp
— exécutable depuis Linux, sans Visual Studio ni PowerShell.

## Périmètre

**Dans :** le portage des scripts de préparation.

**Hors :** toute modification du comportement de la génération. La sortie doit
être identique, octet pour octet.

## Travail

1. Lire `scripts/Prepare-DKR-Runtime.ps1` (34 Ko) et en extraire les étapes
   réelles, en séparant ce qui est intrinsèquement Windows de ce qui l'est par
   commodité d'écriture.
2. Vérifier ce qui existe déjà côté Linux : `scripts/bootstrap_dependencies.py` et
   `scripts/apply-dependency-patches.sh` couvrent vraisemblablement le
   rapatriement et les patchs. Ne réécrire que ce qui manque.
3. Construire l'ELF du decomp de référence sous Linux. Le decomp amont
   (`extern/dkr-decomp`) se construit nativement sous Linux avec une toolchain
   MIPS — c'est son mode d'emploi normal, pas un détournement.
4. Construire N64Recomp et RSPRecomp sous Linux et les exécuter avec les mêmes
   fichiers de configuration : `runtime-recomp/dkr.us.v77.recomp-policy.json` et
   `runtime-recomp/rsp/aspMain.us.v77.toml`.
5. Écrire `Prepare-DKR-Runtime.sh` et `Generate-DKR-RSP.sh`, avec la même
   vérification de ROM et les mêmes messages d'erreur explicites que leurs
   équivalents Windows.
6. Prouver l'équivalence : comparer les empreintes des sources générées sous Linux
   et sous Windows. Une différence est un défaut à corriger, pas une variation
   acceptable — sans quoi les deux chemins divergeront silencieusement.
7. Mettre `docs/BUILDING.md` à jour avec le chemin Linux.

## Critères d'acceptation

- [ ] `Prepare-DKR-Runtime.sh` produit `RecompiledFuncs` et `RecompiledPatches`
      depuis un poste Linux, sans Windows.
- [ ] `Generate-DKR-RSP.sh` produit `RecompiledRSP/aspMain.cpp`.
- [ ] Les sorties Linux et Windows sont identiques par empreinte, ou toute
      différence est expliquée et corrigée.
- [ ] La validation de ROM et les messages d'erreur sont conservés à l'identique.
- [ ] `docs/BUILDING.md` documente le chemin Linux.
- [ ] Les scripts Windows existants continuent de fonctionner.

## Risques

Une divergence non détectée entre les deux chemins de génération produirait deux
jeux de sources différents selon la machine, et donc des bugs qui ne se
reproduisent que chez une personne. La comparaison d'empreintes de l'étape 6
n'est pas un critère de confort : c'est la seule protection contre ce scénario.

## Références

- `Build-Linux.sh:9-12` — le message qui exige Windows
- `docs/BUILDING.md` — prérequis actuels
- `scripts/Prepare-DKR-Runtime.ps1`, `scripts/bootstrap_dependencies.py`,
  `scripts/apply-dependency-patches.sh`
