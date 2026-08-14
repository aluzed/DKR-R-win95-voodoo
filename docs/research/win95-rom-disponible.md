# La ROM était là depuis le début

Constaté le 15 août 2026, après une douzaine d'affirmations contraires.

## Ce qui s'est passé

Le projet attend la ROM à `extern/dkr-decomp/build/dkr.us.v77.z64`. Ce chemin
n'existe pas — `extern/` ne contient que `n64-modern-runtime`. J'en ai conclu que
la ROM était absente, et j'ai bâti plusieurs tickets autour de cette contrainte.

Elle était à `/var/www/Diddy-Kong-Racing/build/dkr.us.v77.z64`, dans le dépôt
voisin dont je lisais `gbi.h`, `f3ddkr.h` et `textures_sprites.c` depuis des
heures pour E05-S03, E05-S06 et E05-S08.

**Un chemin manquant n'est pas un fichier manquant.** Je n'ai jamais cherché par
nom, et j'ai répété la conclusion dans une douzaine de commits et de documents
sans la revérifier une seule fois. Une supposition de départ recopiée devient une
contrainte de fait.

## Ce que cela a coûté

Six tickets ont porté des critères marqués « bloqué par la ROM » qui ne l'étaient
pas. Plusieurs justifications de conception invoquaient l'absence de ROM alors
qu'elles auraient dû invoquer autre chose — la scène synthétique de E09-S02, par
exemple, garde toute sa valeur parce qu'on en connaît la réponse d'avance, et non
parce qu'il n'y avait pas de ROM.

Rien n'est à jeter : les mesures faites restent valides, et les témoins écrits
restent utiles. Ce qui est à corriger, ce sont les raisons données.

## L'état réel, mesuré

La ROM est valide — en-tête `80371240`, titre `Diddy Kong Racing`, code `NDYE`,
12 Mio, empreinte `4f0e07f0eeac7e5d7ce3a75461888d03`.

Et le jeu **démarre sur Windows 95** :

    [boot][input] keyboard: WASD=stick arrows=d-pad Space=A ...
    [boot][rom] validated and registered
    [boot] runtime initialized; waiting for first safe VI state
    [boot][audio] frequency=48000 (diagnostic backend)
    [boot] VI initialized; starting recompiled DKR entrypoint
    [boot][vi] present=60 ... present=2760

Deux mille sept cent soixante images présentées, soit quarante-six secondes à
60 Hz. Une faute de protection apparaît en cours de route — le nombre d'images
avant son apparition varie d'une exécution à l'autre — mais **le fil vidéo lui
survit** et continue de présenter.

Le gestionnaire `[boot][crash]` ne s'est pas déclenché : il ne couvre pas le fil
fautif. C'est le prochain point à traiter.

## Trois obstacles d'outillage levés au passage

**`stderr` n'était pas récupérable.** Tout le journal du runtime y passe, y
compris le gestionnaire de plantage qui imprime code d'exception, adresse et RVA.
Or COMMAND.COM de Windows 95 n'a pas de syntaxe `2>&1`. Le runtime redirige
désormais `stderr` vers un fichier sur cette cible, sans mise en mémoire tampon —
un plantage ne laisse pas le temps de vider un tampon.

Une première version de cette redirection a été posée dans la branche `#else` de
`#ifdef _WIN32`, alors que le point d'entrée sous Windows est `WinMain`. Elle
n'a jamais été compilée, et le symptôme était muet : le programme tournait, le
fichier n'apparaissait pas, rien ne disait pourquoi. L'appel est maintenant dans
`DkrMain`, indépendamment du point d'entrée.

**ScanDisk bloquait chaque démarrage.** Un plantage laisse le volume sale, et
Windows lance ScanDisk au démarrage suivant, où il avale les frappes destinées au
bureau. Le symptôme est déroutant : le programme « ne démarre pas » alors qu'il
n'a jamais été lancé. `AutoScan=0` dans `MSDOS.SYS` supprime la cause. L'image du
disque est sauvegardée en `win95.img.avant-autoscan`.

**La souris est capturée par l'émulateur**, donc un clic en coordonnées absolues
ne signifie rien pour l'invité. Seul le clavier est fiable pour piloter la
machine.
