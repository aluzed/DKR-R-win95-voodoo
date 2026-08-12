# E05-S08 — Filtrage, enveloppement et niveaux de détail

| | |
|---|---|
| **Épic** | E05 — Backend Glide |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E05-S02, E04-S07 |
| **Bloque** | — |

## Contexte

Les modes d'échantillonnage de texture décident d'une part importante de
l'apparence, et la N64 comme la Voodoo ont chacune leurs particularités.

**Le filtrage.** La N64 pratique un filtrage à trois points, sur un triangle de
texels, là où la Voodoo fait un filtrage bilinéaire classique sur quatre. La
différence est réelle et visible sur les textures de faible résolution — c'est-à-dire
sur la plupart de celles de DKR, dont la mémoire de texture de la console était
très limitée. Le rendu ne sera pas identique, et il faut décider quoi en faire.

**L'enveloppement.** La N64 propose l'enveloppement, le bornage et le miroir, ce
dernier étant très utilisé pour économiser de la mémoire de texture. Glide propose
les mêmes modes, avec des contraintes de dimensions.

**Les niveaux de détail.** DKR utilise-t-il des mipmaps ? À vérifier plutôt qu'à
supposer. S'ils sont utilisés, ils consomment un tiers de mémoire de texture en
plus, ce qui pèse sur le budget de E05-S02 ; s'ils ne le sont pas, les textures
lointaines scintilleront, exactement comme sur la console.

## Objectif

Régler les modes d'échantillonnage pour approcher le rendu de la N64 aussi près
que la carte le permet, en connaissant et en documentant les écarts.

## Périmètre

**Dans :** filtrage, enveloppement, miroir, bornage, niveaux de détail.

**Hors :** le décodage des textures (E04-S07) et leur allocation (E05-S02).

## Travail

1. Déterminer si DKR utilise des mipmaps, par examen de l'état RDP relevé en
   E04-S06. Si oui, chiffrer leur coût en mémoire de texture et le remonter à
   E05-S02.
2. Implémenter enveloppement, bornage et miroir, et vérifier que le miroir
   fonctionne pour toutes les tailles de texture utilisées.
3. Régler le filtrage. Comparer le filtrage bilinéaire de Glide au filtrage à trois
   points de la N64 sur des textures de faible résolution, et décider : accepter la
   différence, ou proposer un filtrage au plus proche voisin en option pour un
   rendu plus proche de la console sur certains éléments.
4. Traiter l'interface séparément. Les éléments 2D sont souvent mieux rendus sans
   filtrage — le filtrage rend le texte flou. Vérifier quel mode le jeu demande
   pour ces éléments et le respecter (E05-S07).
5. Mesurer le coût de chaque mode. Le filtrage bilinéaire est gratuit sur Voodoo ;
   le trilinéaire, s'il est utilisé, ne l'est pas et consomme une TMU, ce qui entre
   en conflit avec le multitexturage de E05-S04.
6. Documenter les écarts assumés dans `docs/RENDER-DIFFERENCES.md` : ce qui ne
   ressemblera pas à la console, et pourquoi.
7. Vérifier visuellement sur des surfaces qui révèlent ces réglages : une route
   vue en oblique, une texture répétée en miroir, du texte d'interface.

## Critères d'acceptation

- [ ] L'usage des mipmaps par DKR est déterminé, et leur coût chiffré s'ils sont
      utilisés.
- [ ] Enveloppement, bornage et miroir fonctionnent pour toutes les tailles
      utilisées.
- [ ] Le choix de filtrage est justifié par une comparaison visuelle documentée.
- [ ] Les éléments d'interface respectent le mode demandé par le jeu.
- [ ] Le coût de chaque mode est mesuré, et le conflit éventuel avec E05-S04 est
      identifié.
- [ ] `docs/RENDER-DIFFERENCES.md` documente les écarts assumés.

## Risques

Le filtrage à trois points de la N64 ne peut pas être reproduit exactement sur
Voodoo. C'est une différence irréductible, et elle doit être annoncée plutôt que
subie : si elle est documentée, c'est une caractéristique connue du portage ; si
elle ne l'est pas, elle sera signalée comme un défaut à chaque comparaison.

## Références

- E04-S06 — modes de texture dans l'inventaire RDP
- E05-S02 — budget de mémoire de texture, impacté par les mipmaps
- E05-S04 — conflit potentiel sur l'usage des TMU
