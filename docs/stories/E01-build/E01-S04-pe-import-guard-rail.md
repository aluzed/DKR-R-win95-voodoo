# E01-S04 — Garde-fou : vérification des imports du PE

| | |
|---|---|
| **Épic** | E01 — Chaîne de build 32 bits Windows 95 |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | S |
| **Dépend de** | E01-S03 |
| **Bloque** | E09-S05 |

## État au 2026-08-12 — livré

| Livrable | Fichier |
|---|---|
| Base d'exports versionnée | `tools/win95/exports/*.txt` — **3 201 symboles, 14 DLL** |
| Provenance | `tools/win95/exports/PROVENANCE.md` |
| Exceptions | `tools/win95/exports/exceptions.json` |
| Outil | `tools/win95/check_imports.py` |
| Branchement | post-build bloquant, `cmake/win95-target.cmake` |

**Trois catégories de DLL, et la distinction est le cœur de l'outil :**

| Catégorie | Traitement | Vérifié |
|---|---|---|
| Système | chaque symbole confronté à la base | ✅ bloquant |
| Pilote (`glide2x`, `glide3x`) | signalée, non vérifiable ici | ✅ testé par bibliothèque d'import fabriquée |
| Inconnue | **erreur** | ✅ testé avec `winspool.drv` |

La troisième catégorie est celle qui compte : sans elle, une nouvelle dépendance
passerait inaperçue.

**Le rapport nomme l'objet fautif.** La table d'imports du PE ne conserve pas
cette information — elle est perdue au lien. `--objects` la reconstitue en
relisant les objets avec `nm`. Sur l'épreuve :

```
ABSENT  KERNEL32.DLL:InitializeConditionVariable  <- import_canary.c.obj
```

**Le contrôle est éprouvé par injection, à deux niveaux**, comme celui du jeu
d'instructions : `--self-test` compile un binaire important `GetTickCount64` et
vérifie qu'il est refusé ; `-DDKR_WIN95_SELFTEST_IMPORT=ON` ajoute une unité de
compilation au témoin et **le build échoue**.

Détail instructif : la première version du canari importait `GetTickCount64` et
le build passait — parce que `win95compat` la fournit, et que le lieur résolvait
l'import vers le pont plutôt que vers KERNEL32. L'épreuve échouait à échouer, ce
qui était en soi la démonstration que le pont intercepte correctement. Le canari
importe désormais `InitializeConditionVariable`, que le pont ne couvre pas.

**Une base unique.** L'outil de E00-S01 tenait sa référence dans
`$DKR_WIN95_PREFIX/win95-exports.txt`, hors du dépôt.
`tools/win95/check-win95-imports.sh` est devenu une enveloppe vers le nouvel
outil : deux bases qui divergent seraient pires qu'une seule imparfaite.

**Deux constats versés au passage :**

- **`MSVCRT.DLL` n'est pas d'origine** — datée du 3 novembre 1997 quand tout le
  reste porte le 24 août 1996. Elle arrive avec une mise à jour, et un
  Windows 95 de première génération ne l'a pas. C'est ce qui justifie la liaison
  statique du CRT (ADR 0001).
- **DirectInput est absent.** L'installation porte DirectX 2 — `DDRAW`, `DSOUND`,
  `D3DIM`, `D3DRM` — mais aucun `DINPUT.DLL`. La lecture de manette
  ([E06-S02](../E06-platform/E06-S02-keyboard-and-gamepad-input.md)) doit passer
  par `joyGetPosEx` de `WINMM`, ou le paquet doit embarquer une mise à jour de
  DirectX.

**Reste à faire :** le point 7 du ticket — le même contrôle à l'étape de
packaging — attend [E09-S05](../E09-qa/E09-S05-packaging-distribution.md), qui
n'existe pas encore. L'outil est prêt à y être appelé tel quel.

## État antérieur — l'outil de E00-S01

[E00-S01](../E00-scoping/E00-S01-inventory-of-incompatible-dependencies.md) avait
besoin de ce contrôle pour ses propres mesures, et l'a donc écrit :

- `tools/win95/pe_symbols.py` — tables d'exports et d'imports d'un PE32, sans
  dépendance ;
- `tools/win95/check-win95-imports.sh` — compare les imports d'un binaire à la
  référence, et **renvoie un code de retour non nul** s'il en manque.

La référence n'est pas une liste écrite à la main : `--refresh` l'extrait des six
DLL de `C:\WINDOWS\SYSTEM` de la machine de test (2 754 symboles). Le script
signale aussi séparément les imports `...W` de KERNEL32, qui passent le contrôle
mais sont des stubs inopérants sous 9x.

Reste à faire : l'appeler depuis CMake en post-build, et décider si un manque
casse le build ou se contente d'avertir.

## Contexte

Sous Windows 95, un import manquant est une erreur de chargement : le processus
ne démarre pas du tout. Le symptôme est donc binaire et tardif — on ne le
découvre qu'en lançant le binaire sur la machine cible, ce qui, au rythme d'un
aller-retour vers un émulateur ou une machine réelle, coûte plusieurs minutes à
chaque fois.

Cette classe d'erreur est entièrement vérifiable à froid : la table d'imports du
PE est statique, et la liste des exports de Windows 95 aussi. C'est exactement le
genre de vérification qui doit tourner à chaque build plutôt que dans la tête de
celui qui relit le code.

Le même raisonnement vaut pour le jeu d'instructions, déjà couvert par E01-S01 :
ce ticket ajoute le second garde-fou, celui des symboles.

## Objectif

Refuser à la construction tout binaire qui ne pourrait pas se charger sous
Windows 95.

## Périmètre

**Dans :** l'outil de vérification, sa base de référence, son intégration.

**Hors :** la couche de compatibilité elle-même (E01-S03).

## Travail

1. Constituer la base de référence : les exports de `kernel32`, `user32`, `gdi32`,
   `advapi32`, `winmm`, `ddraw`, `dinput` et `dsound` tels qu'ils existent sous
   Windows 95 OSR2.5. Les extraire des DLL de la machine de test — une liste
   recopiée depuis une documentation est une liste fausse.
2. Verser cette base sous `tools/win95/exports/`, avec la provenance et la version
   exacte du système dont elle est issue.
3. Écrire `tools/win95/check_imports.py` : lecture de la table d'imports du PE,
   confrontation à la base, échec avec la liste des symboles fautifs et la DLL de
   chacun.
4. Traiter le cas des DLL hors système. `glide2x.dll` ou `glide3x.dll` sont
   fournis par le pilote de la carte, non par l'OS : l'outil doit distinguer
   « DLL système, symboles vérifiables » de « DLL fournie, présence à vérifier au
   lancement », et non pas ignorer silencieusement la seconde catégorie.
5. Brancher l'outil en post-build de la cible Win95, en échec bloquant.
6. Prévoir une liste d'exceptions explicites, chaque entrée portant une
   justification écrite — sans quoi la liste devient l'endroit où l'on fait taire
   l'outil.
7. Ajouter le même contrôle à l'étape de packaging (E09-S05), sur le binaire
   réellement distribué.

## Critères d'acceptation

- [ ] La base d'exports est extraite d'un Windows 95 réel et sa provenance est
      documentée.
- [ ] `check_imports.py` détecte un import interdit introduit volontairement —
      testé, pas supposé.
- [ ] Le contrôle est bloquant en post-build et au packaging.
- [ ] Les DLL fournies par un pilote sont traitées à part, pas ignorées.
- [ ] Chaque exception porte une justification écrite.
- [ ] Le rapport d'échec nomme le symbole, sa DLL, et l'objet qui l'importe —
      sans le dernier, le diagnostic reste à faire à la main.

## Risques

Une base d'exports incomplète produit des faux positifs, et des faux positifs
répétés conduisent à désactiver l'outil. Mieux vaut une base restreinte à
quelques DLL, exacte et respectée, qu'une base large et approximative.

## Références

- E01-S01 — vérificateur de jeu d'instructions, même principe
- E01-S03 — couche de compatibilité qui fournit les remplacements
- `scripts/scan_for_game_assets.py` — précédent de contrôle bloquant au packaging
