# E09-S02 — Harnais de comparaison visuelle

| | |
|---|---|
| **Épic** | E09 — Intégration, QA et distribution |
| **Statut** | IN_PROGRESS |
| **Priorité** | P0 |
| **Estimation** | L |
| **Dépend de** | E00-S07, E04-S02, E04-S08, E09-S01 |
| **Bloque** | E05-S03, E08-S03, E09-S04 |

## Contexte

Le portage produit des images. La seule façon de savoir si elles sont justes est
de les comparer à une référence, et de le faire automatiquement — la comparaison à
l'œil ne détecte pas les écarts progressifs, et elle ne passe pas à l'échelle de
plusieurs centaines de scènes.

Trois références sont disponibles, de fidélité décroissante et de commodité
croissante :

1. **La console ou un émulateur de référence** — la vérité, mais lourde à
   instrumenter ;
2. **DKR-R en mode Accurate sur RT64** — conservé comme oracle par l'ADR de
   E00-S07, et facile à piloter ;
3. **Le rastériseur logiciel de E04-S08** — implémente le combineur du RDP
   fidèlement, sans les contraintes de Glide.

Le troisième est le plus utile en pratique, parce qu'il isole une seule variable :
si le rendu logiciel est correct et le rendu Glide faux, le décodeur est hors de
cause et l'erreur est dans le backend. C'est cette isolation qui donne son
efficacité à E05-S03.

La brique centrale est la **capture de display lists**. Une fois qu'on sait
enregistrer les tâches graphiques d'une session et les rejouer, la comparaison
devient déterministe et reproductible — ce qu'une session de jeu jouée à la main
n'est jamais.

## Objectif

Livrer un harnais qui capture, rejoue et compare les images entre les trois
backends, automatiquement.

## Périmètre

**Dans :** capture, rejeu, comparaison, rapport, intégration à la vérification
continue.

**Hors :** la validation sur matériel réel (E09-S04).

## Travail

1. Implémenter la capture des tâches graphiques : `OSTask` et instantané RDRAM
   associé, écrits dans un fichier. Le format doit être stable et documenté, ces
   captures ayant vocation à servir longtemps.
2. Constituer un jeu de captures couvrant le jeu : écran-titre, menus, sélection
   de personnage, un tour sur chaque niveau, cinématiques, écran partagé, écran de
   résultats. C'est le corpus de référence du projet, et sa couverture décide de
   ce que le harnais peut détecter.
3. Implémenter le rejeu : recharger une capture et la soumettre au backend choisi,
   sans faire tourner le jeu. Le rejeu est déterministe, ce qui rend chaque
   comparaison reproductible.
4. Implémenter la comparaison : différence par pixel, avec une métrique tolérante
   à la réduction de profondeur de couleur — Glide rend en 16 bits, une différence
   exacte serait inexploitable. La métrique doit distinguer « quantifié
   différemment » de « faux ».
5. Produire un rapport visuel : image de référence, image obtenue, carte des
   différences, métrique. C'est ce rapport qui rendra E05-S03 praticable.
6. Intégrer à la vérification : une régression visuelle doit se signaler
   automatiquement, avec un seuil par scène plutôt qu'un seuil global — certaines
   scènes sont intrinsèquement plus proches que d'autres.
7. Prévoir le rejeu sur la machine cible, pour comparer le rendu Glide réel à la
   référence obtenue sur l'hôte.
8. Documenter la procédure dans `docs/VISUAL-TESTING.md`.

## Critères d'acceptation

- [ ] Les captures de tâches graphiques sont enregistrables et rejouables.
- [ ] Le corpus couvre titre, menus, tous les niveaux, cinématiques, écran
      partagé et résultats.
- [~] Le rejeu est déterministe pour la scène synthétique — la comparaison des
      comptes de triangles émis précède celle des images, précisément pour que
      l'écart d'image ne masque pas un défaut de déterminisme. **La rejouabilité
      d'une capture réelle reste bloquée** : elle suppose la ROM.
- [x] La métrique de comparaison distingue quantification et erreur — la
      référence est quantifiée en 565 avant comparaison, avec la même
      réplication des bits de poids fort que la relecture, et les pixels de bord
      sont comptés à part. Le seuil large qui a servi à défricher est doublé d'un
      seuil serré une fois le bruit réel mesuré : pire écart 9 sur 255, borne
      posée à 16. Un seuil qu'on ne resserre pas après avoir mesuré n'affirme
      que sa propre indulgence.
- [~] Le rapport présente référence et obtenu en BMP 24 bits ramenés sur l'hôte,
      plus les métriques — surface peinte de part et d'autre, pixels divergents,
      pixels de bord, pire écart et sa position. **L'image de différence n'est
      pas produite par le harnais** ; elle a été calculée sur l'hôte pendant le
      diagnostic.
- [ ] Une régression visuelle est signalée automatiquement, seuil par scène.
- [x] Le rejeu fonctionne sur la machine cible avec le backend Glide — la même
      scène traverse la chaîne complète vers le rastériseur puis vers la Voodoo,
      dont le tampon d'image est relu. Résultat : 0 pixel divergent sur 307200,
      après correction de trois défauts **tous situés dans l'oracle**.
      Voir `docs/research/win95-oracle-vs-carte.md`.
- [ ] Le format de capture est documenté.

> **Correction du 15 août 2026** : ce critère avait été marqué bloqué par l'absence de ROM. La ROM était présente — voir `docs/research/win95-rom-disponible.md`. Le blocage n'existe plus ; ce qui reste à faire l'est pour d'autres raisons, ou n'a simplement pas encore été fait.

## Risques

Un corpus incomplet donne une fausse confiance : ce qu'il ne couvre pas ne sera
pas détecté, et l'absence d'alerte sera lue comme une absence de problème. La
couverture par niveau et par mode de jeu est donc un critère d'acceptation à part
entière, pas un raffinement ultérieur.

## Références

- `docs/RENDER_SNAPSHOT_ARCHITECTURE.md` — instantané par tâche, déjà en place
- E04-S08 — rastériseur de référence
- E00-S07 — conservation de l'oracle RT64
- E05-S03 — consommateur principal du harnais
