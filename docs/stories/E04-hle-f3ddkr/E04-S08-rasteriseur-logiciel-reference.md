# E04-S08 — Rastériseur logiciel de référence

| | |
|---|---|
| **Épic** | E04 — HLE F3DDKR indépendant de RT64 |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | L |
| **Dépend de** | E04-S01, E04-S05, E02-S06 |
| **Bloque** | E05-S01, E09-S02 |

## Contexte

Ce ticket est le pivot de tout l'épic E05, et sa justification tient en une
phrase : quand une image sera fausse en Glide, il faudra savoir si l'erreur vient
du décodeur ou du backend.

Sans oracle intermédiaire, un pixel faux peut venir de dix étages différents —
transformation, découpage, décodage de texture, traduction de combineur, réglage
Glide, pilote. Avec un rastériseur logiciel implémentant la **même interface de
backend** (E04-S01), la question se tranche en une exécution : si l'image logicielle
est correcte et l'image Glide fausse, le décodeur est hors de cause.

Cela en fait aussi le premier jalon **visuel** du projet : la première image du
jeu affichée par ce portage, avant toute ligne de Glide.

## Objectif

Livrer un rastériseur logiciel implémentant l'interface de backend, capable
d'afficher le jeu — lentement, mais correctement.

## Périmètre

**Dans :** rastérisation de triangles et de rectangles, texturage, profondeur,
mélange, sortie vers une image.

**Hors :** toute optimisation. Il est explicitement permis d'être lent : ce
backend est un instrument de mesure, pas un mode de jeu.

## Travail

1. Implémenter l'interface de E04-S01 : gestion d'état, textures, dessin,
   présentation.
2. Implémenter la rastérisation de triangles avec interpolation perspective des
   attributs — couleur, coordonnées de texture, profondeur. La correction
   perspective est indispensable : sans elle, les textures ondulent, et c'est
   précisément le genre d'artefact qu'on cherchera plus tard à imputer à Glide.
3. Implémenter l'échantillonnage de texture selon les modes de E04-S06 :
   enveloppement, miroir, bornage, filtrage point et bilinéaire.
4. Implémenter le combineur du RDP **fidèlement**, sans les contraintes de Glide.
   C'est ce qui donne à ce backend sa valeur d'oracle : il montre ce que l'image
   devrait être, et E05-S03 mesure son écart à cette référence.
5. Implémenter le tampon de profondeur, le mélange et le test alpha.
6. Sortir vers une image en mémoire, affichable par la fenêtre (E06-S01) et
   enregistrable en fichier pour le harnais de comparaison (E09-S02).
7. Le rendre utilisable sur l'hôte moderne comme sur la cible. Sur l'hôte, il est
   assez rapide pour être confortable en développement ; sur la cible, il servira
   ponctuellement au diagnostic.
8. Vérifier contre la cible moderne sur des scènes capturées : l'écran-titre, un
   menu, deux niveaux.

## Critères d'acceptation

- [ ] Le rastériseur implémente l'interface de E04-S01 sans dépendance à Glide.
- [ ] L'interpolation perspective est correcte, vérifiée sur une surface texturée
      vue en oblique.
- [ ] Les modes d'échantillonnage et de filtrage de E04-S06 sont couverts.
- [ ] Le combineur du RDP est implémenté fidèlement, sans contrainte de matériel.
- [ ] Profondeur, mélange et test alpha fonctionnent.
- [ ] Le jeu affiche une image reconnaissable : écran-titre, menu, et une course.
- [ ] Les images produites sont enregistrables en fichier.
- [ ] Il fonctionne sur l'hôte moderne et sur la cible.

## Risques

Le piège est d'y passer trop de temps, ou de céder à la tentation de
l'optimiser. Ce backend n'a pas à être rapide — il doit être **juste** et
**simple**, parce que sa valeur entière tient dans la confiance qu'on lui accorde
comme référence. Un rastériseur optimisé est un rastériseur dont il faut à son
tour vérifier la justesse, et l'oracle disparaît.

## Références

- E04-S01 — interface implémentée
- E04-S06 — modes d'état à honorer
- E09-S02 — harnais de comparaison, consommateur principal
- `../../Diddy-Kong-Racing/docs/stories/E04-hle-f3ddkr/E04-S08-rasteriseur-software-reference.md`
  — même choix d'architecture côté portage natif
