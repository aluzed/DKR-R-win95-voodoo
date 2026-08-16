# Quelle base de temps Windows 95 offre réellement

Mesures de [E02-S03](../stories/E02-system/E02-S03-clock-timers-and-pacing.md),
prises sur la machine de test — Windows 95 OSR2, Pentium II 400 MHz émulé.
Sonde : `tools/win95/witnesses/clock_probe.cpp`.

Le ticket dressait un tableau des sources candidates avec, pour chacune, une
résolution *supposée*. Deux de ces suppositions sont fausses, et elles changent
la conception.

## Le relevé

```text
QueryPerformanceFrequency : 1193180 Hz
timeGetDevCaps            : periode de 1 a 65535 ms

Resolution observee (plus petit pas non nul)
  GetTickCount            : 9 ms
  timeGetTime  (avant)    : 1 ms
  timeGetTime  (apres timeBeginPeriod(1)) : 1 ms
  QueryPerformanceCounter : 5 pas = 4.190 us

Monotonie sur 200000 lectures consecutives
  QueryPerformanceCounter : 0 recul(s)
  timeGetTime             : 0 recul(s)

Cout par appel — ordre de grandeur sous emulation, NON transposable
  GetTickCount            : 45 ns
  timeGetTime             : 6085 ns
  QueryPerformanceCounter : 4685 ns
```

La résolution n'est pas demandée au système, elle est **constatée** : on lit la
source en boucle jusqu'à ce que sa valeur change, et l'écart observé est la vraie
granularité. Une source qui annonce la microseconde et n'avance que toutes les
55 ms est précisément le piège que cette méthode évite.

## Ce que la fréquence dit de l'origine du compteur

`QueryPerformanceFrequency` rend **1 193 180 Hz**. Ce n'est pas une valeur
quelconque : c'est la fréquence du **PIT 8254**, 1,193182 MHz, l'oscillateur à
14,31818 MHz divisé par douze. Windows 95 ne bâtit donc pas `QueryPerformanceCounter`
sur le compteur de cycles du processeur mais sur le minuteur d'intervalle
programmable, lu par accès d'entrée-sortie.

Trois conséquences, et la troisième est la plus importante :

1. **La résolution mesurée, 4,19 µs, vaut 5 pas de PIT.** Ce n'est pas la
   période du compteur — un pas vaut 0,838 µs — mais le temps que prend une
   lecture. On ne peut pas dater plus finement que le coût de la mesure.

2. **Le coût s'explique** : lire le PIT passe par des accès d'entrée-sortie ISA,
   lents par nature. `GetTickCount`, elle, lit une variable en mémoire partagée
   sans changement de contexte, d'où les deux ordres de grandeur d'écart.

3. **Les 32 bits de poids faible rebouclent en exactement 60 minutes.**
   2³² ÷ 1 193 180 = 3 600 s. L'API rend 64 bits et Windows 95 étend le
   compteur, mais c'est un fait qu'il vaut mieux connaître avant qu'après : une
   session de jeu dépasse couramment l'heure.

## Les deux suppositions démenties

### `timeBeginPeriod(1)` ne change rien ici

Le ticket le donnait comme le moyen d'obtenir la milliseconde. `timeGetTime`
rend déjà la milliseconde **avant** tout réglage, et `timeGetDevCaps` annonce une
période minimale de 1 ms.

L'appel est néanmoins fait, et surtout **relâché** : rien ne garantit qu'il en
aille de même sur une autre machine, et sous Windows 9x un réglage laissé en
place dégrade tout le système jusqu'au redémarrage — y compris après la fin du
processus qui l'a posé.

### `GetTickCount` est bien plus fine que 55 ms — et bien moins chère

Le ticket l'annonçait à « ~55 ms », la période du tick DOS à 18,2 Hz. La mesure
donne **9 ms**, et un coût **cent fois moindre** que les deux autres sources.

Elle reste trop grossière pour cadencer 30 images par seconde — un pas de 9 ms
représente plus du quart d'une image — mais elle est le bon outil partout où une
datation grossière suffit, et l'écart de coût est assez large pour que la
question se pose à chaque site d'appel. Le chiffre est versé au budget de
[E08-S01](../stories/E08-perf/E08-S01-frame-budget-instrumentation.md).

> **Réserve.** Les 9 ms sont mesurés sous 86Box. La granularité de
> `GetTickCount` sous Windows 9x dépend du minuteur système, que l'émulateur
> reproduit fonctionnellement et non temporellement. C'est
> [E09-S04](../stories/E09-qa/E09-S04-real-hardware-validation.md) qui tranchera
> sur matériel réel. Le choix de conception n'en dépend pas : `GetTickCount`
> n'est retenue comme source principale dans aucun cas.

## Ce qui a été retenu

`QueryPerformanceCounter` comme source principale — 4,19 µs, monotone sur
200 000 lectures — avec repli sur `timeGetTime` si elle s'avère absente ou
incohérente au lancement. La validation n'est pas une formalité : une source qui
recule, même d'un pas, est écartée au profit du repli plutôt que de produire un
chronomètre qui saute en cours de partie.

Le repli, lui, est un compteur de 32 bits qui reboucle après 49,7 jours. Il est
accumulé par `dkr_tick64_step`, la fonction pure de
[E01-S03](../WIN95-COMPAT.md) — reprise et non recopiée, parce qu'un second
exemplaire du même raisonnement finit toujours par diverger du premier.

Implémentation et contrat : `platform/win95/clock.{h,cpp}`.

## Le défaut que cette mesure a fait apparaître

`ultramodern` dérive `osGetCount` — donc **toute** la mesure du temps de DKR —
de `std::chrono::high_resolution_clock` (`timer.cpp:65`). Reste à savoir quelle
horloge c'est réellement. Mesuré sur la cible :

```text
is_steady=false  system_clock=OUI  steady_clock=non
```

**C'est l'horloge murale.** Sur cette chaîne, `high_resolution_clock` est un
alias de `system_clock`, et `is_steady` vaut faux : elle recule quand
l'utilisateur change l'heure, et quand Windows applique le passage à l'heure
d'hiver. Le compteur de cycles du VR4300, sur lequel reposent la cadence, les
chronométrages de course et la temporisation audio, hérite de ces sauts.

L'ironie est instructive : `timer.cpp` porte, dix lignes plus bas, un
commentaire expliquant que la branche Windows évite `std::chrono::sleep_until`
*précisément* parce que les implémentations « ont été affectées par un recul de
l'horloge système ». La précaution a été prise sur l'attente, pas sur le
compteur.

Ce n'est pas propre à Windows 95 — c'est vrai sur toutes les plates-formes
d'`ultramodern` — mais c'est ici qu'on peut le corriger sans risque, puisque
`platform/win95/clock.{h,cpp}` offre une base monotone dont la dérive est
mesurée. **Brancher `osGetCount` dessus est donc justifié par une mesure et non
par un souci de propreté**, et c'est le point 4 de E02-S03.

## Reproduire

```sh
i686-w64-mingw32-g++-posix -std=c++20 -O2 -march=pentium2 -mno-sse -static \
  -static-libgcc -static-libstdc++ -D_WIN32_WINNT=0x0400 \
  -o CLOCK.EXE tools/win95/witnesses/clock_probe.cpp \
  -Wl,--whole-archive build/win95/libwin95compat.a -Wl,--no-whole-archive -lwinmm
scripts/Push-To-Win95-VM.sh CLOCK.EXE
# dans l'invité : d:\clock.exe — le relevé atterrit dans D:\CLOCK.TXT
```
