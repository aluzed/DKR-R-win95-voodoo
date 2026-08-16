# E03-S03 — Repli : mixeur audio de haut niveau

| | |
|---|---|
| **Épic** | E03 — RSP sur x86 sans SSE |
| **Statut** | TODO — **déclenché** |
| **Priorité** | **P0** |
| **Estimation** | XL |
| **Dépend de** | E03-S02 |
| **Bloque** | — |

## Contexte

**Ce ticket est déclenché.** Sa condition l'est sans ambiguïté :
[E00-S04](../E00-scoping/E00-S04-spike-rsp-cost-without-sse.md) a mesuré que la
cible n'atteint que **3,9 % du débit vectoriel du RSP**. Même en supposant que
l'audio ne consomme que 5 % du RSP sur console, le microcode recompilé coûterait
43 ms par image pour un budget de 33,3 ms — et une implémentation MMX, qui
n'offrirait que quatre voies au lieu de huit, n'y changerait rien.

Détails et chiffres : [`docs/research/rsp-audio-budget.md`](../../research/rsp-audio-budget.md).

Dans ce cas, il faut cesser d'exécuter le microcode instruction par instruction et
interpréter à haut niveau les commandes audio qu'il reçoit : lire la liste de
commandes produite par le moteur, et réaliser directement l'opération demandée —
décodage ADPCM, rééchantillonnage, enveloppe, mixage — en code x86 écrit pour la
machine cible.

C'est l'approche des émulateurs N64 à audio HLE. Elle est nettement plus rapide
et nettement moins fidèle : la sortie n'est plus identique au bit près, et
certains effets propres au microcode de Rare peuvent différer.

Le portage natif voisin a le même arbitrage dans son épic E06, avec le même
raisonnement.

## Objectif

Fournir un mixeur audio de haut niveau qui tienne le budget CPU, avec une
différence sonore mesurée et jugée acceptable.

## Périmètre

**Dans :** l'interprétation des commandes audio et le mixage.

**Hors :** la sortie audio (E06-S03). Le mixeur produit des tampons ; il ne les
restitue pas.

## Travail

1. Documenter l'ABI audio de DKR : format de la liste de commandes, opcodes,
   structures de données. Deux sources se recoupent — le decomp
   (`extern/dkr-decomp`, qui contient le code du moteur audio) et le microcode
   recompilé lui-même, dont le répartiteur est décrit par les seize cibles de
   branchement du fichier TOML.
2. Implémenter les commandes une par une, dans l'ordre de fréquence d'usage. À
   chaque étape, comparer la sortie à celle du microcode recompilé.
3. Traiter en priorité le décodage ADPCM et le rééchantillonnage : ce sont les
   opérations dominantes de tout mixeur audio N64.
4. **Se servir du microcode recompilé comme oracle.** Il compile et s'exécute sur
   la cible par le chemin scalaire — trop lentement pour le temps réel, mais avec
   une fidélité au bit près. C'est la référence contre laquelle mesurer l'écart
   du mixeur, et elle est disponible dès maintenant.
5. Mesurer la différence sonore de façon objective : erreur quadratique moyenne
   par rapport à la référence, sur des séquences musicales et sur des effets.
   « Ça sonne pareil » n'est pas un critère.
6. Mesurer le gain CPU et vérifier qu'il ramène l'audio dans son budget.
7. Conserver le chemin microcode disponible et sélectionnable par la configuration
   (E06-S05) : sur une machine plus rapide que le plancher, la fidélité doit
   rester accessible.
8. Documenter, dans `docs/AUDIO-HLE.md`, ce qui diffère de la référence et
   pourquoi.

## Critères d'acceptation

- [ ] L'ABI audio de DKR est documentée à partir du decomp et du microcode.
- [ ] Le mixeur produit une sortie audible et correcte pour la musique et pour les
      effets.
- [ ] L'écart à la référence est mesuré objectivement et consigné.
- [ ] Le gain CPU est mesuré et ramène l'audio dans son budget.
- [ ] Le chemin microcode reste sélectionnable par la configuration.
- [ ] Les différences connues sont documentées.
- [ ] Le déclenchement de ce ticket est justifié par le chiffre de E03-S02, et ce
      chiffre est cité ici.

## Risques

C'est un des plus gros postes de travail du projet, et il produit une régression
de fidélité assumée. Il ne doit pas être entrepris par confort ou par anticipation
— seulement sur la foi d'une mesure qui prouve que le chemin fidèle ne tient pas.

Inversement, s'il est nécessaire et qu'on le repousse, tout le travail audio en
aval est bâti sur une fondation qui ne tiendra pas.

## Références

- E03-S02 — condition de déclenchement et budget
- `runtime-recomp/rsp/aspMain.us.v77.toml` — répartiteur d'ABI et table de commandes
- `extern/dkr-decomp` — code source du moteur audio du jeu
- `../../Diddy-Kong-Racing/docs/stories/E06-audio/` — même arbitrage côté portage natif
