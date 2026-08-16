# E06-S01 — Fenêtre Win32 et boucle de messages

| | |
|---|---|
| **Épic** | E06 — Plateforme Windows 95 |
| **Statut** | TODO |
| **Priorité** | P0 |
| **Estimation** | M |
| **Dépend de** | E01-S03, E02-S06 |
| **Bloque** | E06-S02, E05-S01, E07-S03 |

## Contexte

SDL2 fournit aujourd'hui la fenêtre, la boucle d'évènements, les entrées et
l'audio. Il disparaît, et il faut réécrire ce qu'il apportait — en Win32 brut,
comme on le faisait en 1996.

Le cas d'usage est heureusement simple, parce que le mode d'affichage l'est.
Sur Voodoo 1 et 2, la carte 3D est un accélérateur *passthrough* : elle prend le
contrôle de l'écran en plein écran, et la fenêtre Win32 ne sert qu'à recevoir les
messages du système — clavier, focus, arrêt. Il n'y a pas de rendu à composer avec
le bureau. Sur Banshee et Voodoo 3, cartes 2D/3D complètes, un mode fenêtré
devient possible, mais il n'est pas indispensable au projet.

Cela simplifie beaucoup : la fenêtre est un récepteur de messages, pas une
surface de rendu.

## Objectif

Livrer `platform/win95/window.{h,cpp}` : création de la fenêtre, boucle de
messages, gestion du focus et de l'arrêt.

## Périmètre

**Dans :** fenêtre, boucle de messages, focus, arrêt, curseur.

**Hors :** les entrées (E06-S02), l'audio (E06-S03), la cadence (E06-S04).

## Travail

1. Créer la classe de fenêtre et la fenêtre. Décider de sa visibilité selon le
   mode d'affichage : sur une carte passthrough, elle peut rester minimale.
2. Écrire la boucle de messages, et décider de son articulation avec la boucle de
   jeu. Le jeu tourne dans les fils gérés par `ultramodern` ; la boucle de messages
   doit tourner sur le fil qui a créé la fenêtre, sans le bloquer et sans
   consommer de CPU inutilement. C'est le point de conception de ce ticket.
3. Traiter le focus. Sous Windows 95, une perte de focus en plein écran accéléré
   demande une décision explicite : mettre le jeu en pause, ou continuer. La pause
   est le comportement attendu.
4. Traiter l'arrêt : fermeture de fenêtre, `Alt+F4`, arrêt du système. Chaque voie
   doit conduire à un arrêt propre, avec restitution de l'affichage (E05-S01) et
   sauvegarde de l'état si nécessaire.
5. Gérer le curseur : masquage en plein écran, restitution à la sortie.
6. Traiter `Alt+Tab` et le basculement de tâche, qui sous Windows 95 avec une
   carte passthrough peut laisser l'affichage dans un état incohérent. Décider du
   comportement — bloquer le basculement, ou le gérer proprement — et le tenir.
7. Installer un filet de sécurité : un gestionnaire d'exception structurée qui
   restitue l'affichage et écrit un journal avant de rendre la main. Sans lui,
   chaque plantage en développement coûte un redémarrage de la machine.
8. Vérifier qu'aucune API postérieure à Windows 95 n'est utilisée — le garde-fou
   de E01-S04 le fait automatiquement.

## Critères d'acceptation

- [ ] La fenêtre se crée et reçoit les messages du système sous Windows 95.
- [ ] La boucle de messages n'interfère pas avec les fils du jeu et ne consomme pas
      de CPU à vide.
- [ ] La perte de focus met le jeu en pause, le retour le reprend.
- [ ] Toutes les voies d'arrêt conduisent à un arrêt propre avec restitution de
      l'affichage.
- [ ] Le curseur est masqué en plein écran et restitué à la sortie.
- [ ] Le comportement au basculement de tâche est décidé et tenu.
- [ ] Un plantage restitue l'affichage et écrit un journal.
- [ ] Aucune API postérieure à Windows 95 n'est importée.

## Risques

L'articulation entre la boucle de messages Win32 et l'ordonnanceur `ultramodern`
est le point délicat. Une boucle de messages qui ne tourne pas assez souvent rend
le système inerte du point de vue de Windows ; une boucle qui tourne trop vole du
temps au jeu. Cet équilibre se mesure (E08-S01), il ne se règle pas au jugé.

## Références

- `runtime-recomp/src/game/runtime_platform.cpp` — intégration SDL actuelle,
  33 Ko, à remplacer
- E05-S01 — mode d'affichage Glide
- E01-S04 — garde-fou d'imports
