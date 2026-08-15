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

`curRSPTask` nul n'est donc pas un accident isolé : **rien n'est renseigné dans
cette structure**. Ce n'est pas une interruption de trop ni une course, c'est un
planificateur qui n'a jamais été rempli — ou une adresse qui n'est pas celle que
le jeu croit.

Les deux pistes qui restent, dans l'ordre où elles se testent :

1. `osCreateScheduler` n'a pas écrit là où le jeu le croit. Le vérifier demande
   de tracer l'appel côté invité, ce que `librecomp` permet par ses exports.
2. La structure est bien à cette adresse mais son contenu a été effacé, par
   exemple par un instantané RDRAM recopié par-dessus — `submit_rsp_task` copie
   8 Mio de RDRAM à chaque tâche graphique, et l'ordre de ces copies mérite
   d'être regardé.

La seconde est bon marché à écarter : la trace montre qu'aucune tâche graphique
n'a encore été soumise au moment de la faute.
