# Fils d'exécution et synchronisation sous Windows 95

Livré par [E02-S01](stories/E02-systeme/E02-S01-couche-threads-synchronisation.md).
Implémentation : `platform/win95/threading.{h,cpp}`.

Même règle que pour [la couche de compatibilité](WIN95-COMPAT.md) : **un
contournement dont la différence n'est pas écrite est un bogue en attente.**
Chaque primitive ci-dessous remplace une primitive standard, et ce qu'elle perd
est écrit — y compris quand elle ne perd rien.

## Ce que la mesure a changé au ticket

Le ticket a été écrit sur trois hypothèses. Le relevé du code d'`ultramodern`
en a démenti deux, et en a révélé une quatrième que personne n'avait vue.

### `ultramodern` n'utilise aucune variable de condition

Le ticket prévoyait le morceau délicat : reconstruire les variables de condition
de Vista sur des événements Windows 95, « un exercice où l'on perd des réveils ».

Recherche faite sur les 15 `.cpp` et 10 en-têtes d'`ultramodern` : **zéro
`std::condition_variable`**. Son attente conditionnelle est un *sémaphore de
comptage* — `moodycamel::LightweightSemaphore` — et rien d'autre.

Le besoin réel, relevé et non déduit :

| Primitive | Usages | Où |
|---|---:|---|
| `std::thread` | 12 | `events.cpp`×6, `threads.cpp`×3, `timer.cpp`×2, `ultramodern.hpp`×1 |
| `std::mutex` | **3 verrous** (5 occurrences avec `lock_guard`) | `events.cpp`, `renderer_context.cpp`, `extensions.cpp` |
| `LightweightSemaphore` | — | `UltraThreadContext::running` et `initialized`, plus chaque `BlockingConcurrentQueue` |
| `thread_local` | 3 | `threads.cpp` : `is_entrypoint_thread`, `is_game_thread`, `thread_self` |
| `this_thread::sleep_for` / `sleep_until` | 2 | `timer.cpp` |
| `std::condition_variable` | **0** | — |

La couche livre donc un **sémaphore**, pas une variable de condition. C'est ce
qui est demandé, et le risque annoncé par le ticket ne se matérialise pas.

### La correspondance des priorités N64 → Win32 n'existe pas, et c'est correct

Le ticket demandait une table de correspondance, en avertissant qu'elle serait
« lossy », la N64 ayant plus de niveaux utiles que Win32 n'en expose.

**Les priorités N64 ne passent jamais par le système hôte.** `ultramodern` tient
l'ordre lui-même, dans une file logicielle : `thread_queue_insert`
(`threadqueue.cpp`) insère chaque `OSThread` en ordre décroissant de `OSPri`, et
**un seul fil de jeu court à la fois** — chacun dort sur son propre sémaphore
`running` jusqu'à ce que l'ordonnanceur le réveille. Windows n'arbitre jamais
entre deux fils de jeu, parce qu'il n'y en a jamais deux prêts en même temps.

Ce qui existe est une autre correspondance, sans rapport avec `OSPri` : les cinq
niveaux d'`ultramodern::ThreadPriority`, qui servent aux fils d'infrastructure.
Elle tient dans les sept classes de Win32 sans en écraser aucune :

| `ThreadPriority` | Constante Win32 | Valeur |
|---|---|---:|
| `Low` | `THREAD_PRIORITY_BELOW_NORMAL` | −1 |
| `Normal` | `THREAD_PRIORITY_NORMAL` | 0 |
| `High` | `THREAD_PRIORITY_ABOVE_NORMAL` | 1 |
| `VeryHigh` | `THREAD_PRIORITY_HIGHEST` | 2 |
| `Critical` | `THREAD_PRIORITY_TIME_CRITICAL` | 15 |

Cinq niveaux dans sept classes : la correspondance est **injective**, et
strictement croissante — deux propriétés que le test vérifie, sur l'hôte comme
sur la cible, parce que `dkr_thread_priority_to_win32` est une fonction pure.

**Ce qui est réellement perdu** est ailleurs, et vaut d'être nommé : les 256
niveaux de `OSPri` sont aplatis par `ultramodern` **sur toutes les plates-formes**,
Windows moderne compris. Ce n'est pas une perte du portage Windows 95 ; c'est un
choix du runtime amont, et la cible Windows 95 n'y ajoute rien.

Détail qui mérite d'être connu : dans `ultramodern`, `set_native_thread_priority`
calcule la constante puis **ne l'applique pas** — l'appel à `SetThreadPriority`
est commenté en amont. La couche, elle, l'applique.

### `CreateSemaphoreW` est un bouchon — le vrai bloquant

C'est la découverte de ce ticket, et elle était invisible aux garde-fous
existants. Elle est traitée en détail dans
[`docs/research/win95-blockers.md`](research/win95-blockers.md) ; en résumé :
`moodycamel::LightweightSemaphore` appelle `CreateSemaphoreW`, que Windows 95
exporte **sans l'implémenter**. Les deux côtés du sémaphore cassent, et
différemment — l'attente ne bloque plus, le signal boucle sans fin.

Le pont de `compat.c` fournit désormais `CreateSemaphoreW`, qui renvoie sur
`CreateSemaphoreA`. La couche de ce ticket, elle, n'appelle que `...A`.

## Les primitives, et ce qu'elles coûtent

### Fils — `_beginthreadex`, et non `CreateThread`

Le ticket disait `CreateThread`. La couche emploie `_beginthreadex`, qui
l'appelle en interne, pour une raison précise : `CreateThread` **ne prépare pas
l'état par fil du CRT** — `errno`, le tampon de `strtok`, l'état de `rand`. Un
fil créé ainsi qui touche au CRT lit et écrit l'état d'un autre fil, et le fuit
en sortant. Les fils d'`ultramodern` y touchent, ne serait-ce que par
`debug_printf` et `std::string`.

L'objection attendue serait la dépendance à `MSVCRT.DLL`. Elle est sans objet :
la table d'imports du témoin de E01-S03 la réclame déjà pour `__getmainargs`,
`_initterm` et une vingtaine d'autres. `_beginthreadex` **est exportée** par la
`MSVCRT.DLL` de la machine de test — vérifié contre sa table d'exports.

**Ce qui est perdu** : rien. Le nom du fil n'est pas posé — `SetThreadDescription`
est de Windows 10 et ne sert qu'à un débogueur, dont la cible n'a pas.

### Détachement — deux allocations, parce qu'il y a deux propriétaires

`dkr_thread_start` alloue **deux** blocs, et c'est la seule façon dont
`dkr_thread_release` puisse être sûre :

| Bloc | Propriétaire | Libéré par |
|---|---|---|
| `dkr_thread` — le descripteur | le créateur | `dkr_thread_join` ou `dkr_thread_release` |
| le paquet de démarrage — `fn`, `arg` | le fil créé | le fil lui-même, dès qu'il l'a recopié |

Les fondre en une seule structure — la version évidente, et la première écrite
ici — est une **utilisation après libération**. Le fil créé lit `fn` et `arg`
comme tout premier geste ; `dkr_thread_release` libère le descripteur sans
attendre. Sur un monoprocesseur, le créateur garde son quantum après
`_beginthreadex` : au moment du `release`, le fil créé n'a en général **pas
encore exécuté une seule instruction**. Il saute ensuite dans un `fn` que le tas
du CRT a déjà recyclé.

Ce n'est pas un cas d'école : `ultramodern/src/timer.cpp` détache son fil de
minuterie immédiatement après l'avoir créé.

Le défaut n'était couvert par aucune épreuve — la suite ne détachait jamais rien.
L'épreuve nº 3 le fait désormais, dans sa forme la plus défavorable : huit fils
démarrés et relâchés aussitôt. Sur le véhicule POSIX sous AddressSanitizer, la
version fusionnée échoue immédiatement en `heap-use-after-free` dans le
trampoline ; la version à deux blocs passe.

### Jonction — `WaitForSingleObject`, et pourquoi ce n'est pas un détail

`std::thread::join()` **ne fonctionne pas sous Windows 95** avec le modèle de
threads `posix`. `pthread_join` de `winpthreads` valide son descripteur par
`GetHandleInformation`, qui est **un bouchon** sous Windows 95 : elle échoue,
`pthread_join` part sur son chemin d'erreur, et libstdc++ transforme cet échec en
`std::system_error`.

Constaté au désassemblage de `libwinpthread_la-thread.o`, puis figé par le
garde-fou : le contrôle des imports refuse désormais `GetHandleInformation`
partout sauf dans `WITNESS.EXE`, le témoin dont le rôle est justement d'exercer
le modèle standard.

`dkr_thread_join` passe par `WaitForSingleObject`, qui fonctionne. **C'est une
des raisons d'être de cette couche**, et elle n'était pas dans le ticket.

### Exclusion mutuelle — non récursive, et vérifiée

Bâtie sur `CRITICAL_SECTION`, c'est-à-dire sur les cinq fonctions que
[E01-S03](WIN95-COMPAT.md) implémente elle-même.

Les sections critiques de Win32 sont **récursives** ; `std::mutex` ne l'est pas.
La différence n'est pas théorique : un code qui comptait sur l'interblocage d'un
`std::mutex` réentrant pour révéler un défaut ne le révélerait plus, et le défaut
passerait en production sur la seule plate-forme où il ne se voit pas.

`dkr_mutex` rétablit la propriété : le propriétaire est noté, et une réentrance
est **détectée et signalée** — journal puis arrêt — au lieu de passer. Le
diagnostic est meilleur que celui de `std::mutex`, qui se contente de figer :
sur une machine émulée sans outillage moderne, un message nommant la faute vaut
beaucoup mieux qu'un gel.

La lecture du propriétaire hors verrou est sûre, et pour une raison précise : la
seule valeur qui déclenche l'alarme est notre propre identifiant, que nous seuls
avons pu y écrire ; l'écriture d'un mot aligné de 32 bits n'est pas déchirable
sur x86. Le test ne peut donc ni manquer une réentrance ni en inventer une.

`dkr_mutex_try_lock` sur son propre verrou rend `0` **sans rien signaler** :
l'appelant d'un `try` a déjà prévu l'échec, comme celui de `std::mutex::try_lock`.

Un cas limite, pour être complet : si un fil se termine en tenant un `dkr_mutex`,
le propriétaire reste inscrit, et un fil ultérieur auquel Windows réattribuerait
le même identifiant serait accusé de réentrance. Le verrou était de toute façon
définitivement abandonné dans ce cas — le programme est déjà cassé, et
l'accusation à tort reste plus lisible qu'un blocage éternel.

**Ce qui est perdu** : les défauts hérités de la `CRITICAL_SECTION` de E01-S03 —
pas de rotation avant blocage, attente de 1 ms plutôt qu'infinie, aucun
diagnostic dans `DebugInfo`. Ils sont décrits dans [WIN95-COMPAT.md](WIN95-COMPAT.md).

### Sémaphore de comptage — la sémantique de réveil, écrite

C'est *le* primitif de blocage du planificateur. Sa sémantique, à comparer à ce
qu'`ultramodern` suppose :

| Propriété | Garantie | Ce qu'`ultramodern` en attend |
|---|---|---|
| `signal(n)` réveille exactement *n* attendeurs | **oui** | oui — un jeton par reprise |
| Un `signal` antérieur au `wait` est conservé | **oui** | **indispensable** — voir ci-dessous |
| Ordre de réveil FIFO | **non garanti** | sans objet — voir ci-dessous |
| Réveil intempestif | **jamais** | oui |

**Le signal qui précède l'attente n'est pas perdu.** C'est la propriété dont
dépend le démarrage des fils de jeu : `osCreateThread` crée le fil puis attend
`initialized`, et le fil créé signale `initialized` avant d'attendre `running` —
mais rien n'ordonne le `signal` de `running` par le créateur et le `wait` du
créé. Un primitif à mémoire nulle perdrait ce réveil et le fil dormirait pour
toujours. Le sémaphore compte, donc il ne perd rien.

**L'ordre de réveil n'est pas garanti, et ce n'est pas un problème.** Windows 95
ne promet pas le FIFO sur un sémaphore. `ultramodern` n'en a pas besoin : chacun
de ses sémaphores `running` n'a **qu'un seul attendeur possible**, le fil
propriétaire du contexte. La question ne se pose donc jamais.

### Événement à réinitialisation manuelle

Ce que le sémaphore ne sait pas exprimer : réveiller **tous** les attendeurs d'un
coup, et rester ouvert pour ceux qui arriveront après — sans que le signaleur ait
à connaître leur nombre. C'est la forme juste d'un signal « une fois pour
toutes » : fin d'initialisation, demande d'arrêt.

`ultramodern` n'en utilise pas aujourd'hui ; il est fourni parce que le sémaphore
ne peut pas le remplacer sans compter les attendeurs, et que E02-S02 aura à
traiter l'arrêt.

### Variables locales au fil — un seul emplacement système

Windows 95 n'offre que **64 emplacements TLS** pour tout le processus, et
`libstdc++` comme `winpthreads` en consomment déjà.

La couche n'en prend donc **qu'un**, qui désigne un tableau de pointeurs : le
nombre de variables par fil devient une affaire de constante du programme
(`DKR_TLS_SLOTS`, 8) et non de ressource système. `ultramodern` en utilise trois.

Le distributeur d'indices est protégé par `lock cmpxchg` et non par un verrou :
`dkr_tls_reserve` peut être appelée avant `dkr_threading_init`, donc avant
qu'aucune section critique ne soit prête.

**Ce qui est perdu** : `dkr_threading_shutdown` ne libère que le bloc du fil
appelant. Il n'existe aucun moyen sous Windows 95 d'aller libérer celui d'un fil
tiers ; appeler cette fonction alors que d'autres fils tournent fuit donc leur
bloc — 32 octets chacun. C'est écrit plutôt que corrigé : la seule correction
possible serait un registre global des blocs, dont le verrou serait pris à chaque
accès TLS. Les blocs des fils créés par la couche sont libérés par eux-mêmes en
fin de vie.

## Vérifier

La suite est **une seule source**, `platform/win95/tests/test_threading.cpp`,
compilée deux fois. Deux fichiers distincts finiraient par diverger, et c'est
sur la cible que les différences comptent.

```sh
platform/win95/tests/run-tests.sh threading           # sur l'hôte
ctest --test-dir build/win95 -R DKRWin95Threading     # idem, via CTest
DKR_STRESS_SECONDS=600 platform/win95/tests/run-tests.sh threading

./Build-Win95.sh
scripts/Push-To-Win95-VM.sh build/win95/bin/THREADS.EXE
# dans l'invité :  d:\threads.exe          puis  d:\threads.exe --stress 600
# le compte rendu complet atterrit dans D:\THREADS.LOG
```

Sur l'hôte, le véhicule est POSIX — `pthread`, `sem_t`. **Ce n'est pas une
plate-forme supportée**, seulement de quoi ramener le cycle « modifier, exécuter,
observer » d'un aller-retour vers la machine émulée à une seconde. Passer là ne
prouve rien de la cible ; c'est pourquoi le même binaire tourne aussi sur la
machine.

**Le délai n'est pas une précaution, c'est le mécanisme de détection.** Un réveil
perdu ne produit pas un mauvais résultat : il produit une attente qui ne finit
pas. Vérifié en injectant la perte d'un réveil sur mille, qui fait expirer le
délai au lieu d'échouer proprement. Sans `timeout`, la suite resterait suspendue.

La suite a été éprouvée par mutation — un test qui ne peut pas échouer ne prouve
rien :

| Mutation injectée | Ce que la suite fait |
|---|---|
| le verrou ne verrouille plus | `ECHEC` — 39 807 incréments sur 40 000 |
| le bloc TLS est partagé par tous les fils | `ECHEC` — 11 lectures croisées, valeur du fil principal perdue |
| un réveil sur mille est perdu | **délai expiré** — l'interblocage attendu |
| descripteur et paquet de démarrage fusionnés | `heap-use-after-free` sous ASan, dans le trampoline |

Le dernier n'est pas une mutation inventée : c'est la version qui a été écrite
d'abord, et que l'épreuve de détachement n'existait pas encore pour attraper.

## Résultats

Sur la machine de test — Windows 95 OSR2, Pentium II 400 MHz émulé :

```text
48 controles, 0 echec(s)
resultat : OK
```

Y compris la vérification que `CreateSemaphoreW` rend un descripteur utilisable,
qui échouerait sur un Windows 95 sans le pont de `compat.c` — c'est-à-dire
exactement ce qu'on lui demande de surveiller.

### Endurance : dix minutes sur la cible

```text
Endurance : 600 secondes
  ...
  598 s, 8420 tours
  ok    8437 tours sans reveil perdu ni interblocage
resultat : OK
```

Chaque tour enchaîne les deux motifs qui peuvent perdre un réveil — l'aller-retour
du planificateur et la contention sur verrou. Sur 8 437 tours, cela représente :

| | Sur la cible, en dix minutes |
|---|---:|
| Réveils de sémaphore (attente + signal) | **8 437 000** |
| Verrouillages sous contention | **337 480 000** |

Aucun réveil perdu, aucun interblocage, et la machine est restée utilisable
pendant toute la durée — ce dernier point n'est pas décoratif : c'est exactement
ce que la première version de `TryEnterCriticalSection` n'avait pas tenu.

La boucle s'arrête **à la première anomalie** et non à la fin du temps imparti ;
elle est donc allée au bout parce qu'elle n'a rien trouvé.
