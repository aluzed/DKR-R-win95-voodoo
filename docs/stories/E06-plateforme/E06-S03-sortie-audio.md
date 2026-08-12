# E06-S03 — Sortie audio

| | |
|---|---|
| **Épic** | E06 — Plateforme Windows 95 |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E03-S02, E02-S03, E06-S01 |
| **Bloque** | E09-S04 |

## Contexte

Le microcode audio produit des tampons (E03-S02) ; il reste à les restituer. SDL2
s'en chargeait ; sous Windows 95, deux voies :

- **`waveOut`**, de `winmm` : simple, universelle, présente sur toute installation.
  Sa latence est élevée, de l'ordre de plusieurs dizaines de millisecondes ;
- **DirectSound**, à partir de DirectX 3 : latence bien plus faible par tampon
  circulaire, mais dépend de la version de DirectX installée et de la qualité du
  pilote de la carte son.

DirectSound est préférable, avec `waveOut` en repli. Sur du matériel de 1998, la
qualité du pilote varie énormément d'une carte à l'autre, et le repli n'est pas
théorique.

Deux contraintes propres à cette cible :

- **le budget CPU.** Le mixage logiciel de DirectSound consomme du processeur, et
  le budget est déjà serré. Une carte son gérant le mixage matériel change la
  donne ;
- **la synchronisation.** Le jeu produit son audio à la cadence de la N64. La
  dérive entre l'horloge du jeu et celle de la carte son doit être absorbée, sans
  quoi le tampon se vide ou déborde au bout de quelques minutes.

Le contrôle du volume, l'égaliseur et les niveaux indépendants existent déjà dans
du code du projet (`audio_equalizer.cpp`, `runtime_audio_controls.cpp`) et sont
conservés — sous réserve de leur coût CPU, à mesurer.

## Objectif

Restituer l'audio du jeu sous Windows 95, sans coupure ni dérive, dans le budget
CPU.

## Périmètre

**Dans :** sortie audio, tampons, synchronisation, contrôle du volume.

**Hors :** la production des tampons (E03-S02) et le mixage HLE (E03-S03).

## Travail

1. Implémenter la sortie DirectSound par tampon circulaire, avec repli `waveOut`.
2. Dimensionner le tampon. C'est l'arbitrage central : un tampon court réduit la
   latence et augmente le risque de coupure quand une image dépasse son budget ;
   un tampon long fait l'inverse. Le régler sur mesure, en tenant compte du 99ᵉ
   centile de temps par image mesuré en E08-S01 — pas de la médiane.
3. Traiter la synchronisation d'horloge : détecter la dérive entre la cadence de
   production du jeu et la consommation de la carte, et l'absorber par le niveau
   de remplissage du tampon plutôt que par rééchantillonnage.
4. Traiter le sous-alimentation du tampon : la détecter, la compter, et la rendre
   visible dans l'affichage de diagnostic (E08-S01). Une coupure audio est le
   symptôme le plus audible d'un dépassement de budget CPU, et c'est un excellent
   indicateur d'ensemble.
5. Mesurer le coût CPU de la sortie, mixage DirectSound compris, et vérifier
   qu'un pilote médiocre ne le fait pas exploser.
6. Vérifier le coût de l'égaliseur (`audio_equalizer.cpp`) sur la cible et statuer
   sur sa conservation. C'est un traitement par échantillon, donc potentiellement
   coûteux sur un Pentium II — s'il pèse, il devient une option désactivée par
   défaut.
7. Vérifier le comportement quand aucune carte son n'est présente ou que le pilote
   échoue : le jeu doit continuer en silence, pas s'arrêter.
8. Vérifier sur une durée longue — au moins vingt minutes de jeu continu — que la
   synchronisation ne dérive pas.

## Critères d'acceptation

- [ ] L'audio est restitué par DirectSound, avec repli `waveOut` vérifié.
- [ ] La taille de tampon est justifiée par le 99ᵉ centile de temps par image.
- [ ] Aucune dérive audible sur vingt minutes de jeu continu.
- [ ] Les sous-alimentations sont comptées et visibles en diagnostic.
- [ ] Le coût CPU de la sortie est mesuré, y compris avec un pilote médiocre.
- [ ] Le coût de l'égaliseur est mesuré, et sa conservation tranchée sur ce chiffre.
- [ ] L'absence de carte son laisse le jeu fonctionner en silence.

## Risques

Sur cette classe de machine, la coupure audio sera le premier symptôme visible
d'un budget CPU dépassé — avant même la chute du nombre d'images par seconde,
parce que l'oreille est plus sensible que l'œil aux irrégularités. Le compteur de
sous-alimentations est donc un instrument de mesure pour tout le projet, pas
seulement pour l'audio.

## Références

- `runtime-recomp/src/game/audio_equalizer.cpp`, `runtime_audio_controls.cpp`
- `runtime-recomp/src/game/audio_mix_policy.hpp`
- E03-S02 — production des tampons
- E08-S01 — budget par image
