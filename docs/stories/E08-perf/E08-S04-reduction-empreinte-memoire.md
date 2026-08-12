# E08-S04 — Réduction de l'empreinte mémoire

| | |
|---|---|
| **Épic** | E08 — Performance |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E00-S06, E08-S01, E07-S01 |
| **Bloque** | E09-S04 |

## Contexte

Sur une machine à 64 Mo, la mémoire n'est pas seulement une contrainte de
capacité : c'est une contrainte de performance. Dès que le jeu dépasse la mémoire
physique, Windows 95 pagine sur un disque de 1998, et le résultat n'est pas une
dégradation progressive mais un effondrement.

E00-S06 a établi le budget et les décisions de réduction. Ce ticket les applique
et vérifie le résultat en fonctionnement réel.

Le poste le plus évident est l'instantané RDRAM : **8 Mio par tâche graphique en
attente**, dimensionné pour un usage — l'interpolation moderne — qui disparaît
avec E07-S01. Sans interpolation, il n'y a plus besoin d'apparier deux images, et
une seule tâche en vol suffit.

## Objectif

Ramener l'empreinte dans le budget de E00-S06, et prouver qu'il n'y a pas de
pagination en cours de partie.

## Périmètre

**Dans :** l'application des décisions de E00-S06 et leur vérification.

**Hors :** la définition du budget lui-même (E00-S06).

## Travail

1. Appliquer la décision sur l'instantané RDRAM : taille réduite à ce que DKR
   utilise réellement, et nombre de tâches en vol ramené à ce qu'exige le profil
   Accurate. Le patch existant
   (`0007-snapshot-rdram-for-queued-graphics-tasks.patch`) est le point d'entrée.
2. Appliquer la décision sur l'accès à la ROM (E02-S04).
3. Revoir les caches côté hôte : textures décodées (E04-S07), display lists,
   tampons de sommets. Chacun doit avoir un plafond explicite plutôt qu'une
   croissance libre.
4. Mesurer la fragmentation du tas sur une session longue. Un jeu qui alloue et
   libère pendant des heures fragmente, et sous Windows 95 le tas ne se compacte
   pas. Si la fragmentation croît, préférer des tampons préalloués aux allocations
   dynamiques dans les chemins chauds.
5. Vérifier l'absence de pagination en fonctionnement : compter les défauts de
   page sur une session de jeu réelle. C'est le critère qui compte réellement — la
   somme des postes peut tenir sur le papier et le système paginer quand même.
6. Vérifier le comportement sur une machine de 32 Mo, configuration de repli
   évaluée par E00-S06 : ce qui se dégrade, et si le jeu reste jouable.
7. Mettre à jour le budget de E00-S06 avec les chiffres réels après réduction.

## Critères d'acceptation

- [ ] L'instantané RDRAM et le nombre de tâches en vol sont réduits selon
      E00-S06.
- [ ] Chaque cache côté hôte a un plafond explicite.
- [ ] La fragmentation du tas est mesurée sur une session longue et ne croît pas
      sans borne.
- [ ] Aucune pagination en cours de partie sur la configuration cible — vérifié
      par comptage de défauts de page, pas par observation.
- [ ] Le comportement sur 32 Mo est évalué et documenté.
- [ ] Le budget de E00-S06 est mis à jour avec les chiffres réels.
- [ ] Aucune régression de comportement du jeu.

## Risques

Réduire l'instantané RDRAM touche à un mécanisme dont
`docs/RENDER_SNAPSHOT_ARCHITECTURE.md` explique qu'il protège la mémoire de
simulation : le décodeur ne doit jamais réécrire dans la RDRAM vivante. Cette
propriété doit survivre à la réduction. La réduire est légitime, la supprimer ne
l'est pas.

## Références

- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md`
- `patches/n64-modern-runtime/0007-snapshot-rdram-for-queued-graphics-tasks.patch`
- E00-S06 — budget et décisions
- E07-S01 — suppression de l'interpolation, qui rend la réduction possible
