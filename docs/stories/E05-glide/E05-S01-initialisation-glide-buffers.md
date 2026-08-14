# E05-S01 — Initialisation de Glide, contexte et tampons

| | |
|---|---|
| **Épic** | E05 — Backend Glide |
| **Statut** | DONE |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E00-S05, E04-S01, E04-S08 |
| **Bloque** | E05-S02, E05-S03, E05-S05, E05-S07 |

## Contexte

Glide est une API de bas niveau, spécifique à 3dfx, sans couche d'abstraction :
elle expose directement le matériel. C'est une bonne nouvelle pour ce projet — la
correspondance avec le RDP est plus directe qu'avec Direct3D de l'époque — mais
cela signifie que chaque détail de configuration compte.

Les points structurants de l'initialisation :

- **Le mode d'affichage.** Sur Voodoo 1 et 2, la carte est un accélérateur
  *passthrough* : elle prend la main en plein écran et n'a pas de mode fenêtré.
  Banshee et Voodoo 3, en revanche, sont des cartes 2D/3D complètes. Cela change
  la nature de l'intégration avec la fenêtre Win32 de E06-S01.
- **Les tampons.** Double ou triple buffering, tampon de profondeur en 16 bits,
  le tout devant tenir dans la mémoire de tampon d'image de la carte — ce qui, sur
  une Voodoo 2, limite les résolutions disponibles.
- **La résolution.** Fixée par l'ADR de E00-S05, avec les résolutions inférieures
  comme repli si le budget de remplissage ne tient pas.

## Objectif

Livrer `platform/render/glide_backend.{h,cpp}` : l'ouverture du contexte Glide, la
configuration des tampons, la présentation, et la fermeture propre.

## Périmètre

**Dans :** initialisation, tampons, échange, fermeture, détection de la carte.

**Hors :** textures (E05-S02), combineur (E05-S03), et tout le rendu.

## Travail

1. Se lier à Glide. Décider entre édition de liens statique à l'import et
   chargement dynamique par `LoadLibrary` : le chargement dynamique permet un
   message d'erreur explicite quand la carte est absente, plutôt qu'un refus de
   chargement par l'OS. Vu que le garde-fou de E01-S04 distingue déjà les DLL
   fournies par un pilote, le chargement dynamique est probablement le bon choix.
2. Implémenter la détection : présence de la bibliothèque, nombre de cartes,
   nombre de TMU, mémoire disponible par TMU et pour le tampon d'image. Ces
   chiffres pilotent E05-S02 et E05-S04 à l'exécution ; ils ne doivent pas être
   codés en dur.
3. Ouvrir le contexte à la résolution retenue, avec le format de tampon de
   profondeur choisi. Vérifier que la configuration demandée tient dans la mémoire
   de la carte détectée, et se replier proprement sinon.
4. Implémenter le cycle d'image : effacement, dessin, échange de tampons. Décider
   entre échange synchronisé sur le balayage et échange immédiat — le premier
   évite le déchirement, le second évite de perdre une image entière quand on rate
   l'échéance, ce qui compte quand le budget est serré. Cette décision se mesure
   (E06-S04).
5. Traduire la fenêtre de ciseaux de E04-S05 vers `grClipWindow`.
6. Implémenter la fermeture : restitution du mode d'affichage, libération du
   contexte. Sur une Voodoo passthrough, une fermeture incorrecte laisse l'écran
   dans un état inutilisable et impose un redémarrage — c'est un défaut très
   pénalisant en phase de mise au point, où les arrêts anormaux sont fréquents.
7. Traiter l'arrêt anormal : installer un gestionnaire qui restitue l'affichage
   même en cas de plantage.
8. Afficher un triangle. C'est le premier pixel Glide du projet, et il vaut
   plusieurs jours de lecture de documentation.

## Critères d'acceptation

- [x] La détection rapporte carte, nombre de TMU et mémoires disponibles —
      relevé sur la machine : 1 carte, 2 TMU, 2048 Ko d'image, 2048 Ko par TMU.
- [x] L'absence de carte produit un message clair, pas un refus de chargement par
      l'OS — **exercé** en retirant la Voodoo de la configuration de l'émulateur,
      puis en la remettant, configuration restaurée octet pour octet. Le chemin
      propre est atteint et `DKR_GLIDE_ERR_NO_BOARD` remonte.
      Mais la mesure a révélé mieux que ce que le critère demandait : **Glide
      affiche sa propre boîte modale anglaise pendant le `LoadLibrary`**, avant
      que notre code ne voie quoi que ce soit. Le registre ne permet pas de la
      devancer — les relevés avec et sans carte sont identiques, et une Voodoo 1
      qui n'a jamais existé y figure comme la carte présente. Le texte d'erreur
      rattache donc explicitement la boîte qui le précède.
      Voir `docs/research/win95-glide-sans-carte.md`.
- [~] Le contexte s'ouvre à la résolution de l'ADR — 640×480, double tampon,
      profondeur — **le repli reste non vérifié** : 86Box ne propose pas de
      Voodoo assez pauvre pour faire échouer 640×480, et la mémoire a toujours
      suffi. Le calcul est écrit et relu, il n'est pas éprouvé. Le
      budget est calculé plutôt que deviné, pour que l'échec dise « 640×480 ne
      tient pas dans 2 Mo » au lieu d'un refus muet.
- [x] Un triangle coloré s'affiche sous Windows 95 sur la cible, **et l'image a
      été relue**. `grLfbLock` contourne l'obstacle de la Voodoo passthrough, dont
      la sortie n'apparaît dans aucune capture de l'émulateur : 75264 pixels
      peints sur 307200, soit exactement l'aire analytique du triangle
      (½ × 448 × 336). Le centre est rouge-dominant, ce qui est la bonne réponse
      et non l'évidente — le centre de l'écran n'est pas le centre de gravité du
      triangle. L'image entière est ramenée en BMP.
- [x] Le cycle d'image tourne à cadence stable — 100 images en 1573 ms, soit
      63 images/s, échange synchronisé sur le balayage. Cela montre que le cycle
      n'est pas le goulot à charge triviale, et rien du taux de remplissage réel.
- [x] La fermeture restitue l'affichage, y compris après un arrêt anormal —
      éprouvé dans les deux sens, le mode « crash » du témoin déréférençant un
      pointeur nul contexte ouvert. Le bureau revient avant la boîte du filtre.
- [x] La fenêtre de ciseaux fonctionne, mesurée plutôt que constatée : un
      triangle symétrique de 112000 pixels, restreint à la moitié droite, en
      garde 55960 sur 56000. Les 40 manquants sont la colonne frontière comptée
      une fois — la fenêtre de Glide est donc incluse à gauche et exclue à droite,
      ce que `backend.h` supposait sans l'avoir vérifié. `dkr_scissor_for_player`
      de E04-S05 fournit les rectangles.
- [x] La couche implémente `dkr_render_backend` de E04-S01 : mélange, profondeur,
      ciseaux, test alpha et brouillard sont traduits, et **les huit modes sans
      texture sont confirmés par relecture du tampon d'image**. Les textures
      restent à E05-S02/E05-S03, et `texture_upload` rend zéro plutôt qu'un
      handle bidon. Voir `docs/research/win95-glide-etats.md`.
- [x] Aucune capacité matérielle n'est codée en dur — TMU et mémoires viennent de
      `grSstQueryHardware`, et le repli de résolution du budget calculé.

## Risques

Glide n'est plus une API vivante : la documentation officielle est d'époque, et
les implémentations disponibles sont les sources ouvertes de 3dfx et leurs forks.
Prévoir du temps de lecture de ces sources — l'écart entre la documentation et le
comportement réel du pilote sera à trancher par l'expérimentation.

Le développement sous émulateur (E09-S01) est indispensable ici : le cycle
« modifier, exécuter, redémarrer la machine » sur du matériel réel serait
insoutenable au rythme où l'on tâtonne à ce stade.

## Références

- [Sources Glide 3dfx](https://sourceforge.net/projects/glide/) ·
  [sezero/glide](https://github.com/sezero/glide) ·
  [hatarch/glide3x](https://github.com/hatarch/glide3x)
- E00-S05 — cible matérielle et version de Glide
- E04-S01 — interface à implémenter
