# Le premier plantage du jeu, et comment il s'est laissé lire

Mesuré le 15 août 2026 sur la machine d'épreuve, avec la ROM.

## Où en est le portage

Le jeu démarre. Le journal du runtime le dit sans ambiguïté :

    [boot][rom] validated and registered
    [boot] runtime initialized; waiting for first safe VI state
    [boot][audio] frequency=48000 (diagnostic backend)
    [boot] VI initialized; starting recompiled DKR entrypoint
    [boot][audio] frequency=22050 (diagnostic backend)
    [boot][vi] present=4260

La ROM est validée, l'entrée du code recompilé est atteinte, et le jeu
reconfigure lui-même la fréquence audio de 48000 à 22050 — c'est-à-dire qu'il
exécute sa propre initialisation, pas seulement celle du runtime.

## Il a fallu trois outils avant de pouvoir diagnostiquer quoi que ce soit

**`stderr` n'était pas récupérable.** Tout le journal y passe et COMMAND.COM de
Windows 95 n'a pas de syntaxe `2>&1`. Le runtime le redirige désormais vers un
fichier sur cette cible, sans mise en mémoire tampon.

**Le jeu n'avait aucun filtre d'exception.** `game_main.cpp` désactive le sien
sur cette cible au motif que `platform/win95/startup.c` « installe déjà son
propre filtre ». C'était vrai du témoin de plate-forme et faux du jeu :
`dkr_win95_startup` n'était appelé que par `witness.c`. Le symptôme était une
boîte « opération non conforme » de Windows, aucune trace, et **le mode vidéo non
restitué** — le plus grave, la Voodoo gardant l'écran par relais analogique.

**ScanDisk avalait les frappes** à chaque démarrage suivant un plantage, ce qui
faisait croire que le programme ne démarrait pas alors qu'il n'avait jamais été
lancé. `AutoScan=0` supprime la cause.

## Premier défaut : la RDRAM était trop petite pour la disposition de librecomp

    *** exception non rattrapee ***
      code    : 0xC0000005 (acces memoire invalide)
      adresse : 0x007B4226        -> o1heapInit + 0x46

L'adresse tombe dans la boucle qui efface les casiers de l'instance
(`out->bins[i] = NULLFRAGMENT`) — c'est-à-dire sur le **premier octet du tas**.

Le correctif 0017 avait dimensionné la RDRAM sur les besoins du *jeu* : le pool
de DKR s'arrête à `RAM_END`, 0x80400000, et le decomp n'emploie pas l'Expansion
Pak. Ce raisonnement est juste sur le jeu et faux sur librecomp, qui place ses
propres régions bien au-dessus :

    0x80800000  poignées PI         8 Mio
    0x80801000  zone de correctifs
    0x81000000  zone de mods       16 Mio

et `init_heap` place le tas à `mod_rdram_start`. Avec 4 Mio engagés et 8 Mio
réservés, cette écriture tombait **hors de la réservation entière**.

La disposition est laissée telle quelle et les tailles la suivent : 20 Mio
engagés placent le tas à 16 Mio avec 4 Mio d'arène utilisable, et 24 Mio réservés
gardent 4 Mio de pages protégées au-dessus, pour qu'une adresse invitée hors
plage continue de déclencher une faute. L'ADR 0003 relève 47 Mio libres sur cette
machine : c'est abordable.

Un `static_assert` interdit désormais de redescendre sous `mod_rdram_start`.

## Second défaut : un pointeur nul dans le gestionnaire RSP du jeu

Le filtre d'exception a été enrichi pour rapporter l'adresse **touchée** et les
registres, et non seulement l'adresse du code. La différence est décisive sur un
portage dont tout l'espace mémoire invité est un tableau indexé :

    adresse : 0x006A98D7        -> __scHandleRSP + 0x97
    touchait: 0x82360010 en lecture
    ecx=00000000   ebp=02360000

L'instruction est `mov -0x7ffffff0(%ebp,%ecx,1),%edx`, la forme typique du code
recompilé : `ebp` porte la base RDRAM, `ecx` l'adresse invitée, et le déplacement
replie le biais KSEG0 avec le champ lu.

`ecx` vaut **zéro**. L'adresse invitée est donc `0x80000010`, et le champ à
l'offset 0x10 d'un `OSScTask` est `list`. Autrement dit :

    sc->curRSPTask->list   avec curRSPTask nul

Le jeu reçoit une fin de tâche RSP **alors qu'il n'a pas de tâche courante**.

Ce n'est pas un défaut du jeu : c'est notre signalisation. Six correctifs de ce
portage portent déjà sur l'ordre d'achèvement SP et DP — 0006, 0009, 0010, 0011,
0012, 0013 — parce que cette zone est délicate. Une interruption SP en trop, ou
délivrée après que le jeu a rendu sa tâche, produit exactement cela.

C'est le prochain point à traiter, et il est désormais **nommé** plutôt que
soupçonné.

## Ce que cette session a changé dans la méthode

Un filtre d'exception qui rapporte l'adresse du code sans l'adresse touchée
oblige à désassembler à la main pour deviner ce qui manquait. Avec les deux, plus
les registres, la faute se lit : ici, trois lignes ont suffi à passer de
« quelque part dans le gestionnaire RSP » à « `curRSPTask` est nul ».

## La trace élimine l'hypothèse la plus probable

`DKR_TRACE_SP` compte les soumissions de tâche et les bords SP et DP. Sur la
machine, avant le plantage :

    [trace][sp] soumis   type=2  soumis=1 sp=0 dp=0
    [trace][sp] sp       type=0  soumis=1 sp=1 dp=0

**Une seule soumission, un seul bord SP**, puis la faute. Il n'y a pas de
livraison en double.

`type=2` est `M_AUDTASK` : la toute première tâche que DKR soumet est **audio**,
pas graphique. Elle part dans la file de commandes du planificateur
(`osSendMesg(osScGetCmdQ(gAudioSched), t)`, `audiomgr.c:363`), donc c'est bien
`__scExec` de libultra qui la démarre — et c'est lui qui pose `sc->curRSPTask`.

Reste l'hypothèse de course : le RSP émulé termine avant que le fil
soumissionnaire n'ait fini sa comptabilité, ce que le matériel réel ne permet
pas — l'interruption y arrive des microsecondes plus tard. Six correctifs de ce
portage portent déjà sur cet ordonnancement, ce qui la rendait plausible.

Elle est fausse. Publier le bord une milliseconde plus tard ne change **rien** :

    adresse : 0x006A98D7        (identique)
    touchait: 0x82360010        (identique)
    ecx=00000000 ebp=02360000   (identiques)

Faute identique, registres identiques. L'état est **déterministe**, pas une
course. Le retard a donc été retiré : un changement qui ne corrige rien mais
modifie l'ordonnancement est pire qu'aucun changement.

Ce que cela laisse : soit `curRSPTask` n'est jamais posé — donc `__scExec` ne
prend pas le chemin qu'on croit — soit il est effacé entre-temps par un second
passage dans `__scHandleRSP` que la trace ne voit pas, celle-ci comptant nos
bords à nous et non les messages que le jeu consomme.

C'est du côté du jeu qu'il faut regarder maintenant, et non du nôtre.

## Le vidage mémoire : la structure est vide

Le filtre vide désormais seize mots depuis tout registre qui ressemble à une
adresse invitée — poids fort `0x80` — en traduisant par la base RDRAM. Le
rapport devient lisible sans attacher un débogueur à une machine qui n'en a pas.

    esi -> 0x80121260 :
      +00  00000100 00000000 00000000 00000000
      +10  00000000 00000000 00000000 00000000
      +20  00000400 00000000 00000000 00000000
      +30  00000000 00000000 00000000 00000000

`esi` est le premier argument de `__scHandleRSP`, donc le `OSSched`. Il est
**presque entièrement nul**. Les deux seules valeurs non nulles, `0x100` en `+00`
et `0x400` en `+20`, ressemblent à des tailles ou des drapeaux, pas à des
pointeurs de file ou de tâche.

> **Cette lecture était fausse, et la correction vaut d'être gardée.** Un
> `OSSched` commence par ses deux modèles de message — `retraceMsg` et
> `prenmiMsg`, 32 octets chacun — puis une file, un tampon, une seconde file, un
> second tampon, et un `OSThread` embarqué de 432 octets. `curRSPTask` vit à
> l'offset **0x274**. Les soixante-quatre octets vidés ne montraient donc que
> l'en-tête, et conclure « la structure est vide » revenait à conclure sur autre
> chose que ce qu'on regardait. Le vidage couvre désormais 640 octets.

L'adresse est la bonne : `eax` vaut `0x024814D4`, soit exactement
`gMainSched + 0x274` une fois retranchée la base RDRAM. Le code lisait bien
`curRSPTask`.

## L'état réel du planificateur

Vidé jusqu'à l'offset 0x280, et lu en inversant chaque mot — la RDRAM invitée est
stockée en octets inversés côté hôte :

| Offset | Champ | Valeur |
|---|---|---|
| 0x260 | `clientList` | **0x80116220** |
| 0x264 | `audioListHead` | 0 |
| 0x268 | `gfxListHead` | 0 |
| 0x26C | `audioListTail` | 0 |
| 0x270 | `gfxListTail` | 0 |
| 0x274 | `curRSPTask` | 0 |
| 0x278 | `curRDPTask` | 0 |

**Le planificateur est bien initialisé** : `clientList` pointe sur un client
enregistré, et l'`OSThread` embarqué est en place. Ce n'est donc ni une structure
vide ni une mauvaise adresse.

Mais **les quatre listes de tâches sont vides**, en plus des deux tâches
courantes. Le message de fin de tâche RSP est arrivé alors que le planificateur
n'avait de tâche **nulle part** — ni en cours, ni en attente.

Cela déplace la question. Elle n'est plus « pourquoi `curRSPTask` a-t-il été
effacé » mais **« pourquoi le RSP a-t-il démarré une tâche que le planificateur
n'a jamais prise dans sa file de commandes »**.

Or `submit_rsp_task` n'est appelé que depuis `osSpTaskStart` de librecomp, et
dans libultra seul `__scExec` l'appelle — après avoir retiré la tâche de `cmdQ`
et l'avoir chaînée dans une des listes. Les listes étant vides, `__scExec` n'a pas
tourné.

Quelque chose démarre donc la tâche sans passer par le planificateur.

Les deux pistes qui restent, dans l'ordre où elles se testent :

1. `osCreateScheduler` n'a pas écrit là où le jeu le croit. Le vérifier demande
   de tracer l'appel côté invité, ce que `librecomp` permet par ses exports.
2. La structure est bien à cette adresse mais son contenu a été effacé, par
   exemple par un instantané RDRAM recopié par-dessus — `submit_rsp_task` copie
   8 Mio de RDRAM à chaque tâche graphique, et l'ordre de ces copies mérite
   d'être regardé.

La seconde est bon marché à écarter : la trace montre qu'aucune tâche graphique
n'a encore été soumise au moment de la faute.

## Deux hypothèses de plus, éliminées

**Les messages SP et DP sont distinguables.** Le planificateur enregistre une
file et un message par événement ; s'ils portaient la même valeur, le jeu
traiterait une fin de DP comme une fin de RSP et entrerait deux fois dans
`__scHandleRSP` — dont le premier passage efface `curRSPTask` avant de le
déréférencer. C'était une explication complète du plantage. Elle est fausse :

    sp.mq=0x801212A0 sp.msg=0x0000029B
    dp.mq=0x801212A0 dp.msg=0x0000029C

Files identiques — c'est bien l'`interruptQ`, à `gMainSched + 0x40` — mais
messages distincts.

**`__scExec` écrit bien `curRSPTask`.** Le code recompilé, à l'adresse invitée
0x8007A030, fait le stockage **dans le créneau de retard** d'un `bne`, donc sur
les deux chemins :

    bne  $s0, $s1, L_8007A038
    sw   $s0, 0x274($t9)      <- créneau de retard, exécuté quoi qu'il arrive

Le champ est donc renseigné après le démarrage de la tâche.

## La pile, et ce qu'elle établit

Windows 95 n'a pas `StackWalk64`, et le code recompilé n'a pas de cadre de pile
exploitable. Le rapport parcourt donc la pile et retient ce qui ressemble à une
adresse de code — pas une pile d'appels exacte, mais une liste de candidats, ce
qui vaut infiniment mieux que rien quand on ignore par où l'on est arrivé.

    0x006AB6A6  __scMain + 0x466
    0x00777101  run_thread_function + 0xE1
    0x007D9DFA  _thread_func + 0x21A
    0x00866CBF  dkr::win95::thread::entry<...> + 0x2F
    0x00834C01  dkr_thread_trampoline + 0x21

La chaîne est confirmée : `__scHandleRSP` est bien appelé depuis `__scMain`, sur
le fil du planificateur, lui-même porté par la couche de threads de E02-S01.

## Ce qui reste, et pourquoi c'est maintenant le suspect principal

Le compte est le suivant : une soumission, un `sp_complete` de notre côté, des
messages distinguables, un `curRSPTask` écrit après le démarrage — et pourtant
`__scHandleRSP` le trouve nul.

Cela ne laisse qu'une possibilité : **le jeu reçoit le message plus d'une fois**.
Notre trace compte nos appels à `sp_complete`, pas les messages effectivement
déposés dans la file invitée. Un dépôt en double serait invisible pour elle.

Le correctif 0013 de ce portage remplace précisément le transport des messages
externes par une file « fiable ». C'est là qu'il faut regarder, et la mesure à
faire est simple : compter les dépôts dans la file invitée, et non les appels qui
les demandent.

## La famine était réelle, et elle est corrigée

Le compteur de dépôts, une fois son plafond rendu **par valeur de message**,
donne la réponse :

    msg=0x0000029B remis   depots=68 remises=1 refus=26
    msg=0x0000029B depose  depots=71 remises=1 refus=35

Un seul `sp_complete`, un seul dépôt — pas de doublon. Mais le bord SP a d'abord
été **refusé et remis en file**, puis déposé neuf refus plus tard. Il était coincé
derrière le flot de retraces dans une file de huit places.

Le jeu, lui, n'attend pas : quelques images sans réponse et son planificateur
abandonne la tâche et remet `curRSPTask` à nul. Notre message arrive après, et
`__scHandleRSP` déréférence un pointeur nul.

Sur le matériel, une interruption SP et un retour de balayage sont deux
événements indépendants dont l'ordre relatif n'est pas garanti. Les servir avant
les retraces est donc fidèle, et suffit à les sortir de la famine — leur ordre
entre eux est préservé, c'est celui-là que le jeu observe.

**Effet mesuré** : le message SP est désormais déposé du premier coup, sans
remise en file. Et la faute **se déplace** vers `__scHandleRDP`, ce qui est la
meilleure preuve que la famine était réelle : le jeu va plus loin et rencontre le
problème suivant.

## Un journal de zéro octet qui contenait tout

Le plantage suivant a produit un `DKRR.LOG` vide — alors que la trace qu'il
contenait était exactement ce qu'on cherchait.

Le runtime redirige `stderr` sans mise en mémoire tampon, donc les octets partent
au système au fil de l'eau. Mais Windows 95 ne met à jour la **taille dans
l'entrée de répertoire** qu'à la fermeture : un processus qui meurt laisse un
fichier de zéro octet dont le contenu est pourtant sur le disque, et invisible
pour tout outil qui lit la table.

Le filtre d'exception ferme donc `stderr` avant d'écrire son propre rapport. Le
journal est passé de 0 à 1446 octets sur le plantage suivant.

## Où en est le compte

Après correction, sur la dernière exécution : quatre retraces, **un** bord SP
déposé du premier coup, **aucun** bord DP jamais déposé — et pourtant une faute
qui passe par `__scHandleRDP` avant de revenir sur `__scHandleRSP`.

Le jeu attend donc une fin de RDP que nous n'émettons pas. La tâche audio de DKR
porte `OS_TASK_DP_WAIT` dans ses drapeaux de tâche, ce qui est la piste à suivre.

## La coalescence des retraces : une correction qui régresse

L'analyse suggérait la suite : puisque la file sature de retraces, ne pas en
déposer un second tant que le premier n'est pas délivré. Sur le matériel, un
retrace manqué pendant que le processeur est occupé est simplement manqué.

**Le jeu ne démarre plus.** Il s'arrête à l'initialisation du tas :

    Initializing recomp heap at offset 0x01000000 with size 0x00400000

et n'affiche plus une seule image. Le drapeau « un retrace attend » reste à un,
et tous les suivants sont écartés : le jeu attend un réveil qui ne vient jamais.

La cause probable est que `dequeue_external_messages` n'est appelé que depuis un
fil invité en attente. Avant que le jeu ne tourne, personne ne draine — le
premier retrace pose le drapeau et rien ne le lève. Auparavant les retraces
s'empilaient dans notre file et étaient délivrés en rafale au premier drainage.

Le changement est retiré. Une correction qui régresse est pire que le défaut
qu'elle vise, et celle-ci échangeait un plantage tardif contre un blocage
immédiat.

Ce que l'échec apprend, et qui vaut d'être gardé : **notre file externe n'est pas
drainée à intervalle régulier**, mais opportunément, quand un fil invité se met en
attente. Toute politique de dépôt qui suppose un drainage périodique est donc
fausse par construction. La bonne forme reste à trouver — probablement en
réservant des places plutôt qu'en écartant des messages.

## Réserver des places plutôt qu'écarter des messages

La forme correcte était contrainte par l'échec précédent : ne rien retenir entre
deux passages, puisque notre file externe n'est pas drainée à intervalle
régulier.

On regarde donc l'état réel de la file invitée **au moment du dépôt**. Si moins
de deux places restent libres, un retrace n'est pas déposé. Sur le matériel, un
retrace levé alors que la file est pleine est perdu de la même façon —
`osSendMesg` y est appelé sans blocage depuis l'interruption.

### Ce que cela change

| | avant | après |
|---|---|---|
| tâches soumises | 1 | **170** |
| bords SP | 1 | **169** |
| bords DP | 0 | **61** |
| listes d'affichage | 0 | **547** |
| voix audio normalisées | 0 | **17** |
| images présentées | ~780 | **3540** |

Le jeu fait tourner son moteur audio et **soumet des listes d'affichage**. C'est
le premier moment de ce portage où DKR fait réellement son travail sur Windows 95.

Le plantage subsiste, plus loin, dans `__scHandleRDP` — mais après 170 tâches au
lieu d'une.

### Ce que la trace disait, et qu'il fallait savoir lire

La saturation ne se voyait pas dans les compteurs de haut niveau : une seule
tâche soumise, un seul bord SP, tout paraissait cohérent. Elle ne s'est révélée
qu'en comptant **les dépôts effectifs dans la file invitée**, et en donnant à
chaque valeur de message son propre plafond de trace — sans quoi le retrace,
soixante fois par seconde, dévorait le budget avant que l'intéressant n'arrive.

## Ce qui reste : le bord DP, et pourquoi il est plus subtil

Le plantage subsiste dans `__scHandleRDP`, avec `curRDPTask` nul lu à l'offset
0x4. Mais il survient désormais **après 170 tâches**, pas après une : c'est une
condition occasionnelle, pas un défaut systématique.

Le decomp éclaire pourquoi c'est plus délicat que le cas SP. À la fin de
`__scHandleRSP`, le planificateur **démarre déjà la tâche suivante** :

```c
state = ((sc->curRSPTask == 0) << 1) | (sc->curRDPTask == 0);
if ((__scSchedule(sc, &sp, &dp, state)) != state)
    __scExec(sc, sp, dp);
```

Et `__scExec` n'écrit `curRDPTask` que lorsque la tâche RSP et la tâche RDP sont
**la même** — le stockage est dans le chemin non pris du `bne`, contrairement à
`curRSPTask` qui est dans le créneau de retard. Une tâche audio, qui ne demande
que le RSP, laisse donc `curRDPTask` inchangé.

Trois causes possibles, qui ne se distinguent pas sans mesure :

1. Nous émettons un bord DP pour une tâche graphique que le jeu n'a pas
   enregistrée comme ayant besoin du RDP. Le chemin graphique appelle
   `dp_complete()` inconditionnellement après `sp_complete()`.
2. Deux bords DP pour une même tâche.
3. La même famine que pour SP, mais résiduelle : la réservation garde deux
   places, or un couple SP+DP en demande exactement deux — si une soumission du
   jeu en prend une entre les deux, le DP est refusé.

La troisième est la plus probable au vu du profil : occasionnelle, et liée à la
pression sur la file. Elle se teste en comptant les refus par source, ce que la
trace sait déjà faire.

Ce qui est acquis en revanche : le jeu soumet 547 listes d'affichage et fait
tourner son moteur audio avant d'y arriver.

## La famine est entièrement résorbée, et ce n'est plus l'explication

Les totaux par message, sur toute l'exécution :

| message | déposés | refusés | remis |
|---|---|---|---|
| retrace | 2984 | **0** | 0 |
| SP | 1274 | **0** | 0 |
| DP | 545 | **0** | 0 |

**Plus un seul refus.** L'hypothèse d'une famine résiduelle sur le bord DP est
donc éliminée : il n'est jamais écarté. Et 545 bords DP pour 550 listes
d'affichage est cohérent — pas de doublement non plus.

Restent donc les causes 1 et 2 : un bord DP émis pour une tâche que le jeu n'a
pas enregistrée comme ayant besoin du RDP, ou une course où le jeu efface
`curRDPTask` par un autre chemin avant que notre message n'arrive.

### Une fausse alerte, et toujours la même cause

Un instant, les chiffres ont paru accuser une multiplication : 1274 dépôts du
message SP pour 172 appels à `sp_complete`. C'était un artefact.

Les lignes `[trace][sp]` cessent d'être imprimées au-delà de quelques centaines
d'événements ; leur dernier affichage montre donc l'état à ce moment-là, pas le
total. Je comparais **un compteur plafonné à un compteur libre**.

C'est la troisième fois de cette enquête qu'un artefact de mesure imite un
défaut, et les trois fois la cause est la même : deux grandeurs comparées sans
que leurs budgets d'observation le soient. La trace garde désormais les deux
formes — les premières occurrences pour la chronologie, les totaux périodiques
pour le reste de l'exécution.

## Un défaut dans mon propre correctif, invisible dans les comptes

La sonde posée pour départager les deux causes restantes n'a **jamais tiré**,
alors que 544 messages DP étaient déposés. Elle se conditionnait sur la source du
message ; la trace des dépôts, elle, se conditionnait sur la valeur.

`enqueue_external_message_src` ne recopiait pas la source dans le message mis en
file. Tout ce qui passe par lui portait donc la valeur par défaut, et **les deux
politiques écrites juste au-dessus manquaient leur cible** :

- la priorité accordée aux bords SP et DP ne promouvait que SP, le seul à passer
  par la variante attendue ;
- la réservation de places destinée aux retraces pouvait écarter un bord DP, qui
  lui ressemblait alors comme un jumeau.

Le défaut ne se voyait pas dans les comptes : rien n'était refusé, donc rien ne
paraissait manquer. Il ne s'est vu que parce qu'une sonde a refusé de tirer.

C'est la quatrième fois de cette enquête qu'un instrument révèle autre chose que
ce qu'il cherchait — et la première où il révèle un défaut du correctif qui le
précède.

## Ce que la sonde établit une fois réparée

    curRDPTask au depot = 0x405F1280  (nuls=0 poses=1)
    ...
    dernier : nuls=0 poses=500

**`curRDPTask` n'est jamais nul au moment où nous déposons le bord DP** — 500
dépôts, aucun nul. Le pointeur est donc effacé **entre notre dépôt et le
traitement par le jeu**.

Ce n'est donc ni un message perdu, ni un message de trop, ni un message pour une
tâche qui n'a pas besoin du RDP : les trois hypothèses tombent. C'est une
transition d'état du jeu lui-même, entre la mise en file et la prise en charge.

La piste suivante est dans `__scHandleRSP` : il termine en appelant `__scExec`
pour démarrer la tâche suivante, et `__scExec` n'écrit `curRDPTask` que lorsque
la tâche RSP et la tâche RDP sont la même. Une tâche RSP seule, démarrée entre
notre dépôt et son traitement, laisse donc `curRDPTask` à la valeur qu'y a mise
le dernier `__scHandleRDP` — c'est-à-dire zéro.

À l'état actuel : 555 listes d'affichage, 3720 images, aucun message refusé.

## L'écart entre les deux bords n'est pas la cause non plus

Le chemin graphique publie le bord SP **avant** d'analyser la liste d'affichage —
c'est le choix du correctif 0009 — et le bord DP après. L'écart couvre donc tout
le temps de rendu, pendant lequel le jeu peut démarrer plusieurs tâches, puisque
`__scHandleRSP` termine en appelant `__scExec`.

Hypothèse plausible, et fausse. Accoler les deux bords ne change **rien** :

    adresse : 0x006AA58C   -> __scHandleRDP + 0x6c   (identique)

Le changement a été retiré : il modifie l'ordonnancement sans bénéfice, et le
correctif 0009 existe pour une raison.

## L'état de l'enquête

Cinq hypothèses éliminées par la mesure, dans l'ordre où elles ont paru les plus
probables :

1. livraison en double du bord SP — un seul dépôt pour un seul appel ;
2. course d'ordonnancement — retarder le bord ne change rien ;
3. structure du planificateur non initialisée — elle l'est, je regardais son
   en-tête au lieu du champ ;
4. messages SP et DP indistinguables — leurs valeurs diffèrent ;
5. famine résiduelle sur le bord DP — plus aucun message n'est refusé ;
6. écart entre les bords SP et DP — l'accoler ne change rien.

Ce qui est établi : `curRDPTask` est **toujours renseigné au moment où nous
déposons** le bord DP, et nul quand le jeu le traite. L'effacement se produit
donc dans le jeu, entre la mise en file et la prise en charge, et aucune des
politiques de livraison testées ne l'influence.

La prochaine mesure doit donc porter sur le jeu et non sur nous : instrumenter
`__scExec` et `__scHandleRDP` côté invité pour voir quelle transition efface le
champ. Le code recompilé ne se prête pas au `printf`, mais l'adresse de
`gMainSched + 0x278` est connue — une surveillance de cette case, échantillonnée
depuis le runtime, dirait quand elle passe à zéro.

## DKR envoie ses tâches dans la file d'interruptions

`rcp_dkr.c` ne passe pas par la file de commandes du planificateur :

```c
osScInterruptQ = osScGetInterruptQ(sc);      /* gfxtask_init */
...
osSendMesg(osScInterruptQ, dkrtask, OS_MESG_BLOCK);
```

Les tâches graphiques partent donc **directement dans la file d'interruptions**,
où `__scMain` les récupère par son cas `default:` et les chaîne avec
`__scAppendList`.

Cette file de huit places porte ainsi quatre choses à la fois : nos retraces,
nos bords SP et DP, les messages internes du planificateur, et les pointeurs de
tâche du jeu — ces derniers en envoi **bloquant**.

Cela explique rétroactivement l'ampleur de l'effet de la réservation de places.
Ce n'était pas seulement une interruption retardée : quand la file saturait, le
fil graphique du jeu **se bloquait** en tentant de soumettre sa tâche. Le modèle
que j'avais en tête — « nos messages retardent les siens » — était trop
optimiste : nos messages *arrêtaient* le jeu.

## L'autre écart relevé au passage

`func_80079760`, l'ajout de Rare au planificateur, appelle `__scYield` dès que de
l'audio attend pendant qu'une tâche RSP tourne. DKR **interrompt donc sa tâche
graphique** pour laisser passer l'audio.

Notre runtime ignore les yields : `osSpTaskYield` est vide et `osSpTaskYielded`
rend toujours zéro, avec le commentaire « agit comme si la tâche s'était terminée
avant de recevoir la demande ». Le chemin `if (osSpTaskYielded(...))` de
`__scHandleRSP` n'est donc jamais pris, et une tâche interrompue est traitée comme
achevée.

Ce n'est pas nécessairement la cause du plantage restant, mais c'est un endroit
où le modèle du jeu et le nôtre divergent franchement, sur un mécanisme que DKR
emploie réellement.

## Le yield n'est pas emprunté

`func_80079760` appelle `__scYield` dès que de l'audio attend pendant qu'une
tâche RSP tourne, et notre runtime ignore les yields. La divergence est réelle
dans le code ; reste à savoir si le jeu l'emprunte.

Compteurs posés dans `osSpTaskYield_recomp` et `osSpTaskYielded_recomp` :
**aucune des deux fonctions n'est appelée** sur une séquence de 577 listes
d'affichage.

Septième hypothèse éliminée. La divergence existe mais dort — elle pourrait se
réveiller en course, où l'audio est plus chargé, et il faudra y repenser à ce
moment-là. Elle n'explique pas le plantage actuel.

Les compteurs ont été retirés : garder une sonde permanente sur un chemin mort
coûte un correctif de dépendance pour rien. Le raisonnement, lui, reste consigné
ici — c'est ce qui évitera de refaire la mesure.

## Bilan de l'enquête

Sept hypothèses éliminées par la mesure, chacune ayant paru la plus probable au
moment d'être testée :

| # | Hypothèse | Ce qui l'a écartée |
|---|---|---|
| 1 | livraison en double du bord SP | un dépôt pour un appel |
| 2 | course d'ordonnancement | retarder le bord ne change rien |
| 3 | planificateur non initialisé | il l'est ; je lisais son en-tête |
| 4 | messages SP et DP confondus | leurs valeurs diffèrent |
| 5 | famine résiduelle sur DP | plus aucun message refusé |
| 6 | écart entre les bords SP et DP | les accoler ne change rien |
| 7 | yield ignoré | le jeu ne yield pas |

Deux causes réelles trouvées et corrigées en chemin : la RDRAM trop petite pour
la disposition de librecomp, et la saturation de la file d'interruptions — cette
dernière **bloquant** le fil graphique du jeu, puisque DKR y envoie ses tâches en
`OS_MESG_BLOCK`.

Ce qui reste établi et non expliqué : `curRDPTask` est toujours renseigné quand
nous déposons le bord DP, et nul quand le jeu le traite.

## Huitième élimination, et une contradiction qui tient

Si `curRDPTask` est renseigné au dépôt et nul au traitement, et que seul
`__scHandleRDP` l'efface, alors un autre bord DP a dû être traité entre les deux
— donc en attendre un dans la file.

Compté directement, en parcourant les messages vivants de la file invitée à
chaque dépôt : **aucun bord DP n'est jamais déjà en attente**. Zéro collision sur
toute l'exécution.

La contradiction tient donc, et elle est maintenant précise :

- `curRDPTask` est non nul à **chaque** dépôt du bord DP ;
- aucun second bord DP n'attend jamais dans la file ;
- `__scHandleRDP` le trouve pourtant nul.

Aucune des trois affirmations n'est une supposition : chacune est mesurée.

### Une erreur d'affichage dans la sonde, sans conséquence sur la conclusion

Les valeurs relevées — `0x405F1280`, `0xB05F1280` — ne ressemblent pas à des
pointeurs KSEG0, qui commencent par `0x80`. Inversées, elles donnent
`0x80125F40` : mon inversion d'octets était à l'envers dans l'affichage.

La conclusion « non nul » ne dépend pas de l'ordre des octets et tient donc. Mais
la valeur imprimée était fausse, et je ne l'ai remarqué qu'en relisant. Une sonde
qui affiche une valeur invraisemblable mérite qu'on s'arrête sur
l'invraisemblance avant de se servir du résultat — c'est ce qui avait sauvé la
mesure Z contre W, où un profil impossible avait révélé un artefact de mise en
route.

### Ce qu'il faut mesurer ensuite

La seule façon de trancher est de voir la case changer. Une surveillance
échantillonnée de `gMainSched + 0x278` à chaque bascule de fil invité donnerait la
chronologie exacte de son passage à zéro — c'est plus intrusif que tout ce qui a
été fait ici, et c'est désormais la seule question ouverte.

## Neuvième et dixième éliminations

**Le chemin RDP seul n'est jamais emprunté.** `__scExec` a deux écritures de
`curRDPTask`, et ma première lecture du code recompilé n'avait vu que la
première :

```c
if (sp) { ...; sc->curRSPTask = sp; if (sp == dp) sc->curRDPTask = dp; }
if (dp && (dp != sp)) { osDpSetNextBuffer(...); sc->curRDPTask = dp; }
```

Le second chemin programme le RDP seul, et `osDpSetNextBuffer_recomp` est un
`assert(false)` — **qui disparaît en compilation optimisée**. Le jeu y
programmerait donc un travail dont aucun bord DP ne viendrait jamais. Compteur
posé : **aucun appel** sur 532 listes d'affichage. Le cas ne se présente pas.

**Aucune livraison en double.** Comptés au point de passage unique, `do_send` :

| message | envoyés | reçus | écart |
|---|---|---|---|
| retrace | 2804 | 2804 | 0 |
| SP | 1019 | 1019 | 0 |
| DP | 427 | 427 | 0 |

### La même erreur de mesure, une quatrième fois — mais vue à temps

Une mesure intermédiaire annonçait +463 retraces, +5 SP et +2 DP reçus de plus
qu'envoyés. Deux DP de trop suffisaient à expliquer le plantage, et la
conclusion était à portée de main.

Elle était fausse : je comptais les dépôts dans `dequeue_external_messages`, qui
n'est **qu'un** des chemins menant à `do_send`, tandis que je comptais les
réceptions sur tout. Le jeu envoie lui aussi certains de ces messages. Compter en
amont d'un entonnoir, c'est compter une branche en croyant compter le tout.

Les trois fois précédentes, l'erreur a coûté une conclusion fausse. Celle-ci a
été vue avant, parce que l'écart sur le retrace — 15 % — était trop gros pour un
mécanisme de duplication ponctuel. **L'invraisemblance de l'ordre de grandeur est
le garde-fou qui a servi le plus souvent dans cette enquête.**

## L'hypothèse qui reste, et comment la tester

Notre bord SP est publié avant le rendu. Le jeu peut donc, entre notre SP et
notre DP, démarrer la tâche graphique suivante — `__scExec` y pose
`curRDPTask` = **la nouvelle tâche**. Notre DP, destiné à l'ancienne, arrive
alors : `__scHandleRDP` prend la nouvelle, la termine prématurément et remet le
champ à zéro. Le DP de la nouvelle tâche trouve ensuite un pointeur nul.

Cela expliquerait le caractère occasionnel, et pourquoi accoler les deux bords
n'a pas suffi — `__scExec` peut encore s'intercaler.

Le test : étiqueter chaque bord DP avec la tâche à laquelle il correspond et
comparer à `curRDPTask` au moment du traitement. Nos messages ne portent qu'une
valeur constante ; il faut donc l'étiquette de notre côté, et la comparaison au
moment du dépôt.

## Onzième élimination : le bord DP vise la bonne tâche

L'hypothèse : notre bord SP étant publié avant le rendu, le jeu démarrerait la
tâche graphique suivante entre nos deux bords, et notre DP terminerait la
mauvaise.

Testée en étiquetant chaque bord DP avec la tâche à laquelle il correspond — la
tâche d'ordonnancement vaut l'adresse de l'`OSTask` moins 0x10 — et en la
comparant à `curRDPTask` au moment du dépôt :

    curRDPTask=0x80125F40 attendue=0x80125F40 OK
    curRDPTask=0x80125FB0 attendue=0x80125FB0 OK
    ...
    (concordent=300 divergent=0)

**Trois cents concordances, aucune divergence.** Les deux adresses alternent, ce
qui est cohérent avec un double tampon de tâches graphiques.

L'écart de comptage relevé plus haut s'explique sans défaut : le message de
retrace emprunte un chemin qui ne passe pas par notre file externe, de sorte que
la trace posée dans `dequeue_external_messages` le manquait là où `do_send` le
voyait. Encore un effet de sonde placée ailleurs qu'au point de passage.

## Où en est l'espace des causes

Onze hypothèses éliminées, toutes par la mesure :

| # | Hypothèse | Écartée par |
|---|---|---|
| 1 | double livraison du bord SP | un dépôt par appel |
| 2 | course d'ordonnancement | retarder ne change rien |
| 3 | planificateur non initialisé | il l'est |
| 4 | messages SP et DP confondus | valeurs distinctes |
| 5 | famine résiduelle sur DP | plus aucun refus |
| 6 | écart entre les bords | les accoler ne change rien |
| 7 | yield ignoré | jamais appelé |
| 8 | bord DP déjà en attente | jamais |
| 9 | chemin RDP seul | jamais emprunté |
| 10 | livraison en double | envois = réceptions |
| 11 | bord DP mal dirigé | 300 concordances sur 300 |

Ce qui reste établi et sans explication : `curRDPTask` désigne la bonne tâche au
dépôt, aucun message n'est perdu ni dupliqué, et `__scHandleRDP` le trouve
pourtant nul.

Toutes les sondes posées jusqu'ici observent **nos** chemins ou l'état du jeu
**à nos moments**. La seule mesure qui reste échantillonne l'état du jeu **à ses
moments à lui** : surveiller `gMainSched + 0x278` à chaque bascule de fil invité.
C'est un travail d'instrumentation d'un autre ordre, et c'est par là qu'il faut
reprendre.

## Pris sur le fait

La sonde qui manquait ne regardait pas nos chemins mais **l'instant du jeu** :
dans `do_recv`, juste après que la boucle du planificateur a retiré un message de
sa file, et juste avant qu'elle ne reparte dans son gestionnaire.

    [trace][rcv] msg=667 curRSP=0x80111338 curRDP=0x00000000   (bons=1)
    ...
    [trace][rcv] msg=668 curRSP=0x00000000 curRDP=0x00000000 <== NUL
                                            (bons=1157 nulsSP=0 nulsDP=1)

**Une seule occurrence sur 1157 réceptions**, et c'est la dernière ligne du
journal — donc le plantage lui-même.

Le détail qui compte : **les deux champs sont nuls**. Pas seulement `curRDPTask`.
Le jeu n'a plus aucune tâche en cours, ni RSP ni RDP, quand notre bord DP arrive.

### Ce que cela change

Toutes les hypothèses précédentes cherchaient pourquoi `curRDPTask` était effacé
alors qu'une tâche était en cours. La question est mal posée : **le jeu est au
repos**. Les deux gestionnaires ont fait leur travail, les deux champs sont
remis à zéro, aucune tâche n'attend — et un bord DP arrive quand même.

Sur le matériel, le RDP au repos ne signale rien. Notre bord est donc de trop,
et le compte global ne le montre pas parce qu'il y a **moins** de bords DP que de
listes d'affichage : ce n'est pas un doublon, c'est un bord **tardif**.

L'explication cohérente avec tout ce qui est mesuré : notre fil graphique est
asynchrone. Il retire une tâche, publie SP, rend, puis publie DP. Si le jeu a
entre-temps termine la tâche par un autre chemin — le bord SP suffit à la faire
avancer, et `__scHandleRetrace` peut la conclure — alors notre DP arrive dans le
vide.

Cela explique aussi la rareté : il faut que le jeu conclue la tâche avant que le
rendu ne se termine, ce qui n'arrive que sur une image particulièrement longue.

### La mesure qui reste à faire

Comparer, pour ce bord DP fautif précisément, la tâche qu'il visait avec l'état
du jeu. La sonde d'étiquetage existe déjà et rapportait 300 concordances sur
300 — mais elle mesure **au dépôt**, et le cas fautif se produit **à la
réception**. Il faut donc étiqueter le message lui-même, ou consigner l'attendue
au moment du dépôt pour la relire à la réception.

## L'état de la tâche visée, et une correction posée au mauvais endroit

L'étiquette portée jusqu'à la réception donne le tableau complet du cas fautif :

    msg=668 curRSP=0x00000000 curRDP=0x00000000 <== NUL
    visee=0x80125FB0 state=0x00000001 flags=0x00000023

`state = 1` vaut `OS_SC_NEEDS_RDP` : **la tâche attend encore le RDP**. Elle n'a
donc pas été conclue — l'hypothèse du bord tardif sur une tâche déjà terminée
tombe. Le planificateur ne lui avait simplement **jamais accordé le RDP** :
`__scExec` ne pose `curRDPTask` que lorsque la tâche RSP et la tâche RDP sont la
même, et quand le RDP est occupé au moment de l'ordonnancement, la tâche démarre
sur le RSP seul.

Correction tentée : différer le bord DP tant que `curRDPTask` ne désigne pas
notre tâche, en réutilisant la file de réessai.

**Sans effet.** Et la raison se lit dans les mesures déjà faites : au dépôt,
`curRDPTask` désigne toujours la bonne tâche — trois cents fois sur trois cents.
La condition de report n'est donc jamais vraie. L'écart naît **entre le dépôt et
la réception**, et une garde posée au dépôt ne peut rien y voir.

C'est la même erreur de placement que celle commise cinq fois sur les sondes,
transposée à un correctif : **agir là où l'on observe, plutôt que là où le
phénomène se produit.**

La garde devrait être à la réception — mais nous n'avons pas la main sur le
moment où le jeu retire son message, sauf à altérer la sémantique de sa file.
Une autre forme reste à trouver : par exemple ne publier le bord DP qu'une fois
que le jeu a effectivement accordé le RDP, ce qui suppose d'attendre côté fil
graphique plutôt que de déposer et différer.

## La cause : un chien de garde, pas un ordonnancement

Douze hypothèses éliminées, toutes sur l'**ordre** des messages, toutes fausses —
parce que le défaut n'est pas un défaut d'ordre. Il fallait cesser de lire notre
code et lire le sien.

`__scHandleRetrace`, dans `libultra/src/sc/sched.c` du décomp, porte un chien de
garde que la libultra d'origine n'a pas. Rare l'a ajouté :

```c
if (sc->curRDPTask) gCurRDPTaskCounter++;

if ((gCurRDPTaskCounter > 10) && (sc->curRDPTask)) {
    if (sc->curRDPTask->unk68 == 0) {
        osSendMesg(sc->curRDPTask->msgQ, &gBootBlackoutMesg, OS_MESG_BLOCK);
    }
    set_curRDPTask_NULL = TRUE;
    gCurRDPTaskCounter = 0;
    osDpSetStatus(DPC_SET_XBUS_DMEM_DMA | DPC_CLR_FREEZE | DPC_CLR_FLUSH |
                  DPC_CLR_TMEM_CTR | DPC_CLR_PIPE_CTR | DPC_CLR_CMD_CTR);
}
...
if (set_curRDPTask_NULL) { sc->curRDPTask = NULL; }
```

Au-delà de dix retraces avec une tâche RDP en cours, le jeu **déclare le RDP
planté** : il remet `curRDPTask` à zéro et réinitialise les registres DP — mais
il **n'efface pas `OS_SC_NEEDS_RDP` sur la tâche**. Notre bord DP, arrivant
après, entre dans `__scHandleRDP`, y lit `curRDPTask == 0` et déréférence zéro.

C'est mot pour mot l'état que la sonde avait capturé :

    msg=668 curRSP=0x00000000 curRDP=0x00000000 <== NUL
    visee=0x80125FB0 state=0x00000001 flags=0x00000023

`curRDPTask` nul **et** la tâche visée portant encore `NEEDS_RDP`. Deux chemins
seulement mettent `curRDPTask` à zéro, et `__scHandleRDP` efface `NEEDS_RDP`
quand il le fait. Le chien de garde est le seul qui laisse cette combinaison
derrière lui. La mesure désignait la cause depuis le début ; c'est sa lecture qui
manquait.

### Le seuil se compte en retraces, pas en millisecondes

Premier jet de ce diagnostic : « dix retraces valent 167 ms, et un rendu logiciel
Glide sur un Pentium II émulé dépasse ce budget. » La mesure embarquée avec le
correctif le **réfute** — pire cas **60 ms sur plus de mille images, zéro
dépassement**.

L'hypothèse était plausible, elle expliquait le symptôme, et le correctif qu'elle
a inspiré fonctionne. Trois raisons de ne pas la vérifier, et elle est fausse
quand même. C'est précisément le cas où l'on n'aurait pas mesuré.

**Avec la réserve qui compte, et qui a failli manquer** : cette construction
emploie le **rendu nul**. Les 60 ms sont le seul parcours de la liste
d'affichage, sans une ligne rastérisée. La mesure réfute donc l'hypothèse *pour
ce binaire*, pas pour un rendu Glide réel — lequel coûtera bien davantage et fera
revenir la question du budget. C'est un argument de plus pour publier le bord
tôt : non pas un palliatif, mais la seule structure qui tienne quand le rendu
s'alourdira. Et 60 ms de simple parcours produisent déjà six retraces par liste ;
onze n'est pas loin, sans rien avoir dessiné.

Ce que le retard coûte n'est pas du temps mais des **retraces traités entre
temps** : `gCurRDPTaskCounter` est remis à zéro par `__scExec` au démarrage d'une
tâche, puis incrémenté une fois par `__scHandleRetrace`. Publier le bord DP après
`send_dl` laisse tout un rendu de retraces s'intercaler.

Les totaux de la course le chiffrent : **3238 messages VIDEO contre 567
RDP_DONE**, soit environ six retraces par liste d'affichage. Confortablement sous
onze en moyenne — et pas sous onze dans la queue de distribution. Une seule
excursion suffit, ce qui explique que le plantage ait frappé **une fois**, à la
liste 344 d'une course qui avançait par ailleurs.

### Le correctif

Publier le bord DP **avant** le rendu plutôt qu'après, exactement comme le bord
SP l'est déjà — et pour la même raison, écrite deux lignes plus haut dans
`events.cpp` : la tâche en file possède un instantané immuable de la RDRAM, donc
le rendu ne lit jamais de mémoire que le jeu pourrait recycler, et l'échange vers
l'écran nous appartient, pas au jeu.

Le correctif emporte la mesure qui le justifie : durée de `send_dl`, pire cas, et
compte des images au-delà des 167 ms. Sans elle, le diagnostic serait plausible
au lieu d'être vérifiable.


### Vérifié sur la machine

Même ROM, même construction par ailleurs :

| | listes d'affichage | présentations | réceptions nulles | vidage de plantage |
|---|---|---|---|---|
| avant | 344 puis EXCEPTION | — | 1 | 2618 octets |
| après | 1135 et croissant | 7020 | **0** | aucun (156 octets) |

### Deux pièges d'observation levés au passage

**Le journal d'un programme figé fait zéro octet.** Windows 95 ne met la taille à
jour dans le répertoire qu'à la fermeture, et son cache à écriture différée
retient les secteurs. Le filtre d'exceptions contourne cela en fermant le flux
avant d'écrire — mais un programme qui **bloque** n'atteint aucun gestionnaire.
On lit alors « zéro octet » et on conclut « le programme n'a rien produit, donc
il s'est arrêté tôt », alors qu'il tourne peut-être normalement. `dkr_diag_commit`
appelle `_commit`, c'est-à-dire `FlushFileBuffers`, qui force le cache **et**
l'entrée de répertoire ; appelée de loin en loin, elle rend le journal lisible en
vol. C'est ce qui a permis de lire la course à 570 listes puis à 1135 sans
l'interrompre.

**`DKR_TRACE_SP` ne survit pas à la boîte « Exécuter ».** Il n'est pas dans
`autoexec.bat`, donc un lancement depuis le menu Démarrer donne un journal vide —
indiscernable d'un programme muet. Le jeu se lance désormais par `D:\RUNDKR.BAT`,
qui pose la variable avant d'appeler l'exécutable.

**« Ne répond pas » ne veut pas dire figé.** Windows 95 affiche cet avertissement
dès qu'un fil porteur de fenêtre ne dépile pas ses messages. Le jeu tourne dans
son propre fil et ne dépile rien : l'avertissement est donc **normal**, et l'avoir
lu comme un blocage a coûté un aller-retour de plus. Trois observations, trois
lectures fausses, toutes du même genre : conclure d'une absence de signal.
