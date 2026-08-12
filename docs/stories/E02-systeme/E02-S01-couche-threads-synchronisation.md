# E02-S01 — Couche de fils d'exécution et de synchronisation Win95

| | |
|---|---|
| **Épic** | E02 — Substrat système Windows 95 |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | ~~L~~ **M** |
| **Dépend de** | E01-S02, E01-S03 |
| **Bloque** | E02-S02, E02-S03, E06-S03 |

## État au 2026-08-12 — périmètre réduit par la mesure

[E00-S01](../E00-cadrage/E00-S01-inventaire-dependances-incompatibles.md) a
chiffré ce ticket, et il est plus petit que prévu :

- **`ultramodern` n'a que 6 fichiers concernés**, 12 `std::thread` et 5
  `std::mutex` au total. Il se patche ; il ne se réécrit pas.
- Le manque tient en **six fonctions** : `TryEnterCriticalSection`, `GetThreadId`
  et les quatre variables de condition de Vista.
- **Choisir le modèle de threads `posix` (winpthreads)** plutôt que `win32` :
  même nombre de bloquants, mais superficiels — `IsDebuggerPresent` renvoie faux,
  `SetProcessAffinityMask` ne fait rien, `GetTickCount64` s'enveloppe autour de
  `GetTickCount`, les gestionnaires vectorisés se rabattent sur
  `SetUnhandledExceptionFilter`, qui existe. Reproduire les variables de
  condition de Vista est nettement plus délicat.
- La livraison visée est une **petite bibliothèque de compatibilité** placée
  avant `libkernel32.a` dans l'ordre de résolution du lieur, pas une couche
  d'abstraction dans `ultramodern`.
- `std::atomic` est acquis : vérifié à l'exécution sur le Pentium II émulé.

Détail et chiffres : [`docs/research/win95-blockers.md`](../../research/win95-blockers.md).

## Contexte

`ultramodern` reproduit l'ordonnanceur de la N64 : plusieurs fils du jeu à
priorités strictes, plus les fils d'infrastructure du runtime. Il s'appuie sur la
bibliothèque standard C++ moderne — `std::thread`, `std::mutex`,
`std::condition_variable`, et vraisemblablement des primitives C++20.

Sous Windows 95, deux étages posent problème :

- l'implémentation de ces primitives par la bibliothèque standard peut appeler des
  API absentes du système (E00-S01 les a listées) ;
- Windows 95 ne connaît pas les variables de condition, qui datent de Vista, ni
  `TryEnterCriticalSection`, ni `SignalObjectAndWait`.

Un point souvent oublié joue en notre faveur : la machine cible est
**monoprocesseur**. Il n'y a pas de parallélisme réel, seulement de
l'entrelacement. Les courses restent possibles — la préemption est réelle — mais
tout un pan de complexité liée aux modèles mémoire faibles disparaît.

## Objectif

Livrer `platform/win95/threading.{h,cpp}` : une interface minimale de fils et de
synchronisation, bâtie uniquement sur des API présentes dans Windows 95, sur
laquelle `ultramodern` sera reposé en E02-S02.

## Périmètre

**Dans :** fils, exclusion mutuelle, attente conditionnelle, événements, variables
locales au fil, et leur validation.

**Hors :** l'ordonnanceur N64 lui-même (E02-S02) et les patchs `ultramodern`.

## Travail

1. Définir l'interface à partir de ce dont `ultramodern` a réellement besoin —
   relevé dans son code, pas déduit d'un modèle générique. Le surdimensionnement
   coûte ici directement en travail de portage.
2. Implémenter les fils sur `CreateThread` : création, terminaison, jonction,
   priorité. Faire correspondre les priorités N64 aux classes de priorité de
   thread Win32, et écrire la table de correspondance : la N64 a plus de niveaux
   utiles que Win32 n'en expose, la correspondance est donc lossy et doit être
   choisie explicitement.
3. Implémenter l'exclusion mutuelle sur `CRITICAL_SECTION`. Vérifier le
   comportement de la réentrance sous Windows 95 : les sections critiques Win32
   sont récursives, ce que `std::mutex` n'est pas — un code qui s'appuyait sur
   l'interblocage d'un `std::mutex` non récursif pour révéler un défaut ne le
   révélera plus.
4. Implémenter l'attente conditionnelle. Sans variable de condition native, le
   schéma est un événement à réinitialisation manuelle par attendeur, plus un
   compteur protégé. Écrire explicitement quelle garantie de réveil est offerte —
   un attendeur, tous, ordre respecté ou non — et la faire correspondre à ce
   qu'`ultramodern` suppose.
5. Implémenter les variables locales au fil sur `TlsAlloc`. Windows 95 limite
   sévèrement le nombre d'emplacements : compter ceux qui sont réellement utilisés
   et n'en allouer qu'un, indexant une structure, si le compte est serré.
6. Écrire les tests : création et jonction, exclusion sous contention, réveil
   conditionnel sans réveil perdu, respect de l'ordre de priorité. Ces tests
   doivent tourner sur l'hôte moderne **et** sur la cible ; un test de
   synchronisation qui ne tourne que sur l'hôte ne prouve rien de la cible.
7. Passer les tests sous stress : boucle longue sous charge, dans la machine de
   test, pour faire sortir les réveils perdus. Une exécution de dix secondes ne
   trouve pas ce genre de défaut.

## Critères d'acceptation

- [ ] `platform/win95/threading.{h,cpp}` n'importe aucune API absente de
      Windows 95 — vérifié par le garde-fou de E01-S04.
- [ ] L'interface couvre les besoins relevés dans `ultramodern`, sans surplus.
- [ ] La correspondance des priorités N64 → Win32 est écrite et justifiée.
- [ ] La sémantique de réveil de l'attente conditionnelle est documentée et
      correspond à ce qu'`ultramodern` suppose.
- [ ] La différence de réentrance entre `CRITICAL_SECTION` et `std::mutex` est
      documentée, et son effet sur le code appelant évalué.
- [ ] Les tests passent sur l'hôte moderne et sous Windows 95 émulé.
- [ ] Une exécution de stress d'au moins dix minutes passe sans réveil perdu ni
      interblocage.

## Risques

Les défauts de synchronisation sont rares, non déterministes, et se manifestent
sous forme de gel aléatoire en cours de partie. Ils sont particulièrement coûteux
ici, parce que le cycle de diagnostic passe par une machine émulée sans outillage
moderne. D'où l'insistance sur les tests de stress plutôt que sur la relecture.

## Références

- `docs/ARCHITECTURE.md` — `ultramodern` fournit l'ordonnancement
- E00-S01 — API de synchronisation manquantes
- E01-S03 — couche de compatibilité
