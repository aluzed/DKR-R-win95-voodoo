# E01-S03 — CRT, démarrage et couche de compatibilité d'API

| | |
|---|---|
| **Épic** | E01 — Chaîne de build 32 bits Windows 95 |
| **Statut** | REVIEW |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E00-S01, E01-S01 |
| **Bloque** | E01-S04, E02-S01, E06-S01 |

## État au 2026-08-12 — livré et exécuté sur la machine

`platform/win95/` : `compat.{h,c}`, `tick64.c`, `startup.{h,c}`, `witness.c`.
Sémantique perdue documentée dans [`docs/WIN95-COMPAT.md`](../../WIN95-COMPAT.md).

Le témoin exerçant **la couche entière** tourne sous Windows 95 :

```
IsDebuggerPresent      : faux
SetProcessAffinityMask : accepte
GetTickCount64         : 123 ms ecoulees        (pour un Sleep de 120 ms)
TryEnterCriticalSection: verrou libre pris
deux fils, 4000 tours  : compteur = 4000 / 4000
```

et son journal de démarrage identifie le système :

```
systeme : plate-forme 1, version 4.0 build 1111
 C
```

soit `VER_PLATFORM_WIN32_WINDOWS`, 4.0 build 1111 marqueur « C » — la signature
de Windows 95 OSR2.

**Le rebouclage de `GetTickCount` est simulé, pas attendu.** La logique est
isolée en fonction pure dans `tick64.c` ; neuf vérifications la pilotent avec des
valeurs choisies, dont deux rebouclages successifs et une propriété de monotonie.
Exécuté par CTest sur l'hôte (`DKRWin95Tick64`), sans émulateur.

**Deux contournements que le ticket anticipait n'ont pas eu à être écrits**, et
c'est mesuré plutôt que supposé : ni `SignalObjectAndWait` ni
`InitializeCriticalSectionAndSpinCount` n'apparaissent dans les imports de
`libwinpthread`, `libstdc++` ou `libgcc`. **Le risque annoncé par le ticket — perdre
l'atomicité de `SignalObjectAndWait` — ne se matérialise pas.**

**Unicode.** `librecomp` travaille en `std::u8string`, donc en octets UTF-8 : 236
usages de chaînes étroites contre 25 de chaînes larges, toutes des `u8string` et
non des `wchar_t`. Il n'y a pas de conversion large à supprimer. **Une exception
subsiste** — `mod_manifest.cpp:52` appelle `_wfopen_s`, qui n'est pas exportée par
le `MSVCRT.DLL` de la machine. C'est dans le système de mods, déjà désigné comme
premier candidat au fork.

**CRT : liaison statique**, décision de l'ADR 0001. Conséquence écrite pour
E09-S05 : le paquet n'a aucun redistribuable à embarquer pour le CRT.

**Une découverte de conception :** le garde `_WIN32_WINNT=0x0400` posé par
E01-S01 masque aussi les API que *cette couche fournit* — il ne fait pas la
différence entre une API utilisée par inadvertance et une API remplacée.
`compat.h` doit donc redéclarer ce qu'il implémente, sous condition de version.
Constaté en compilant le témoin, qui a échoué sur `GetTickCount64`.

## Contexte

Un binaire peut compiler, se lier, et refuser de démarrer sous Windows 95 pour
deux raisons qui n'apparaissent nulle part dans les journaux de build :

1. Il importe un symbole absent de la `kernel32.dll` de Windows 95. Le chargeur
   refuse alors le processus **avant** toute exécution, avec un message qui nomme
   au mieux la DLL. Le code de démarrage du CRT est le premier suspect : il
   s'exécute avant `main` et dépend d'API que les runtimes récents supposent
   acquises.
2. Il dépend d'une DLL absente. `msvcrt.dll` n'est pas fourni par le Windows 95
   de première génération — il arrive avec OSR2, avec Internet Explorer 4, ou
   avec une application qui l'installe.

E00-S01 a inventorié les manques et E00-S02 a validé un exécutable témoin. Ce
ticket transforme ce témoin en fondation utilisable par tout le projet.

## Objectif

Livrer `platform/win95/` : le démarrage, la stratégie de CRT, et une couche de
compatibilité qui fournit les API manquantes — de sorte qu'aucun autre ticket
n'ait à s'en préoccuper.

## Périmètre

**Dans :** démarrage, CRT, remplacement des API manquantes, redistribuables.

**Hors :** les fils d'exécution et la synchronisation (E02-S01), la fenêtre
(E06-S01).

## Travail

1. Trancher la stratégie de CRT selon le résultat du témoin T2 de E00-S02 :
   édition de liens statique — binaire plus gros, aucune dépendance — ou
   `msvcrt.dll` redistribué. Écrire la décision et sa conséquence sur le
   paquet de distribution (E09-S05).
2. Écrire `platform/win95/compat.h` et `compat.c` : une implémentation pour chaque
   API manquante relevée en E00-S01. Les cas attendus, chacun à confirmer par la
   mesure :
   - `TryEnterCriticalSection` — repli sur une section critique classique, ou sur
     un objet mutex nommé si la sémantique non bloquante est réellement requise ;
   - `InitializeCriticalSectionAndSpinCount` — `InitializeCriticalSection`, le
     paramètre de rotation étant sans objet sur un monoprocesseur ;
   - `GetTickCount64` — `GetTickCount` sur 32 bits, avec détection de
     débordement ; le compteur repasse à zéro après 49,7 jours, ce qui ne se
     rencontre pas en test et se rencontre chez un joueur ;
   - `SignalObjectAndWait` — décomposition non atomique, avec une note explicite
     sur la fenêtre de course ainsi ouverte ;
   - variables de condition — construction sur événements et section critique.
3. Traiter le cas Unicode. Sous Windows 9x, les API `...W` sont des stubs qui
   échouent. Imposer les API `...A` et les chaînes en pages de code, y compris
   pour les chemins de fichiers. Vérifier ce que `librecomp` fait des chemins.
4. Écrire le point d'entrée : `WinMain` ou `main`, initialisation du CRT, capture
   des exceptions structurées, et un journal de démarrage écrit dans un fichier —
   il n'y a pas de console utilisable pour diagnostiquer sur la machine cible.
5. Ajouter un contrôle de version au lancement : refuser proprement un système
   antérieur au plancher retenu, avec un message compréhensible plutôt qu'un
   plantage.
6. Documenter chaque contournement dans `docs/WIN95-COMPAT.md`, avec la
   sémantique exacte perdue par rapport à l'API d'origine. Un contournement dont
   la différence n'est pas écrite est un bug en attente.

## Critères d'acceptation

- [ ] `platform/win95/compat.{h,c}` couvre toutes les API manquantes de E00-S01.
- [ ] Chaque contournement documente la sémantique perdue dans
      `docs/WIN95-COMPAT.md`.
- [ ] Le débordement de `GetTickCount` est traité et couvert par un test qui
      simule le passage à zéro.
- [ ] La stratégie de CRT est tranchée, et sa conséquence sur la distribution
      écrite.
- [ ] Le point d'entrée écrit un journal de démarrage dans un fichier.
- [ ] Un système trop ancien est refusé par un message clair.
- [ ] Un exécutable témoin utilisant l'ensemble de la couche démarre sous
      Windows 95 émulé.

## Risques

Un contournement qui affaiblit la sémantique d'une primitive de synchronisation
produit des courses rares, non reproductibles, et découvertes très tard. C'est
notamment le cas de `SignalObjectAndWait`, dont l'atomicité est précisément la
raison d'être. Si `ultramodern` en dépend, la décomposition n'est pas une
solution acceptable — il faut revoir l'appelant, pas imiter l'appelé.

## Références

- `docs/ARCHITECTURE.md`
- E00-S01 — inventaire des API manquantes
- E00-S02 — témoins T1 à T3
