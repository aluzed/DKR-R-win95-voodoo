# Ce qui arrive au joueur sans carte 3dfx

Mesuré le 14 août 2026 en retirant la Voodoo 2 de la configuration de
l'émulateur (`voodoo = 0`), puis en la remettant — configuration restaurée
octet pour octet, vérifiée par empreinte.

## Glide affiche sa propre boîte, et rien ne permet de la devancer

`dkr_glide_detect` charge `glide2x.dll` par `LoadLibrary` précisément pour que
l'absence de carte devienne une phrase plutôt qu'un refus de chargement par le
système. Cela protège bien contre l'absence de la *bibliothèque*. Contre
l'absence de la *carte*, non :

    _GlideInitEnvironment: glide2x.dll expected Voodoo, none detected

Ce texte n'est pas le nôtre. Il vient de l'initialisation de la DLL, qui
s'exécute pendant le `LoadLibrary` lui-même, et il s'affiche dans une **boîte
modale** que le programme ne voit pas venir. Le joueur reçoit donc un message
anglais signé d'une bibliothèque dont il n'a jamais entendu parler, avant
d'atteindre notre chemin d'erreur.

La boîte a aussi une conséquence qu'on n'attend pas : elle vole le focus, ce qui
a bloqué l'arrêt de la machine d'épreuve et laissé le volume de transfert marqué
sale. Une boîte modale imprévue ne gêne pas que la personne devant l'écran.

## Le registre ne permet pas de savoir

L'idée naturelle est de vérifier la présence de la carte *avant* de charger la
DLL. `glide_registry_probe.c` a relevé le registre dans les deux configurations,
avec et sans carte.

Le résultat est net : **les deux relevés sont identiques**.

    Enum\PCI
      VEN_121A&DEV_0001   instance BUS_00&DEV_0C&FUNC_00
                          ConfigFlags=0x00000000  "Voodoo2 3D Accelerator"
      VEN_121A&DEV_0002   instance BUS_00&DEV_0C&FUNC_00
                          ConfigFlags=0x00000000  "Voodoo2 3D Accelerator"
    Software\3Dfx Interactive\Voodoo2        present
    C:\WINDOWS\SYSTEM\glide2x.dll            present

Windows 95 conserve dans `Enum` les périphériques qu'il a connus, et il ne marque
pas ceux-ci comme retirés : `ConfigFlags` vaut zéro dans les deux cas. La clef
logicielle du pilote survit évidemment au retrait de la carte, puisqu'elle
appartient au pilote.

Un détail achève de discréditer le critère : `VEN_121A&DEV_0001` figure dans les
deux relevés alors que **cette carte-là n'a jamais existé sur cette machine**.
C'est un reliquat de la configuration Voodoo 1 corrigée par E00-S05, et le
registre le présente exactement comme la carte réellement présente. Se fier à
`Enum` aurait donc produit un faux positif sur la machine même qui a servi à
écrire le test.

Lire l'espace de configuration PCI directement demanderait un VxD, ce qui est
hors de proportion avec le bénéfice.

## Ce qui est fait à la place

La boîte ne peut pas être évitée ; elle peut être **rattachée**. Le texte de
`DKR_GLIDE_ERR_NO_BOARD` mentionne désormais explicitement le message anglais qui
le précède, pour que le joueur lise un seul problème au lieu de deux :

    aucune carte 3dfx detectee — c'est ce que disait aussi le message anglais
    de glide2x.dll

Vérifié sur la machine sans carte : après la boîte, le programme reprend la main,
`grSstQueryHardware` échoue, et `DKR_GLIDE_ERR_NO_BOARD` remonte jusqu'au témoin.
La séquence complète est donc : boîte anglaise incompréhensible, puis explication
française qui la désamorce. Ce n'est pas idéal, et c'est le maximum atteignable
sans réécrire le pilote.

## Ce qui reste non exercé

Le repli de résolution. La mémoire de la carte a toujours suffi, et 86Box ne
propose pas de configuration de Voodoo assez pauvre pour forcer l'échec de
640×480. Le calcul de budget est écrit et relu, il n'est pas éprouvé.
