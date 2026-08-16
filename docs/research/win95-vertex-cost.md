# Ce que coûte un sommet sur la cible

Mesure de [E04-S03](../stories/E04-hle-f3ddkr/E04-S03-matrices-and-vertex-transform.md),
14 août 2026, sur la machine de test — Pentium II 400 MHz, Windows 95.

## Le chiffre

```text
mesure : 20000 sommets x 20 passes en 273 ms
soit 0.682 us par sommet en virgule flottante x87
soit 24322 sommets dans une image de 16,6 ms
```

Transformation complète : matrice modèle-vue-projection appliquée, division
perspective, mise à l'échelle vers l'écran, écriture directe au format du
backend — `dkr_render_vertex`, qui a la disposition de `GrVertex`.

## Ce que ce chiffre autorise à dire

Une image à 60 Hz laisse 16,6 ms. **24 322 sommets** y tiennent, si l'on ne fait
que les transformer.

C'est le budget brut, et il faut le lire comme tel : la transformation n'est pas
seule dans l'image. Il reste le décodage de la display list, le découpage,
l'émission vers Glide, et le remplissage — qui est le poste dont E08 s'occupera.

## Ce que ce chiffre ne dit pas

**Il ne tranche pas entre virgule flottante et virgule fixe**, et le ticket
demandait de trancher sur une mesure. Seul le x87 est mesuré ici.

Écrire une variante en virgule fixe à la hâte donnerait un chiffre trompeur : la
multiplication 16.16 demande des intermédiaires sur 64 bits, la division
perspective ne s'y fait pas naturellement, et une implémentation approximative
mesurerait sa propre maladresse plutôt que la technique. Mieux vaut un chiffre
manquant qu'un chiffre faux.

Et la décision ne bloque rien pour l'instant : elle dépend du **nombre de sommets
par image dans une vraie scène**, que seul le jeu peut donner — donc la ROM. Si
DKR émet deux à quatre mille sommets par image, le x87 coûte 1,4 à 2,7 ms, soit
8 à 16 % du budget : significatif, pas fatal. Si la scène en émet vingt mille, la
question change de nature.

**C'est E08-S01 qui fournira ce nombre**, et c'est à ce moment-là que la mesure
en virgule fixe vaudra d'être faite.

## Protocole

Vingt passes sur vingt mille sommets, soit quatre cent mille transformations.
Les passes multiples ne sont pas une précaution de style : `GetTickCount` avance
par pas de **9 ms** sur cette machine (E02-S03), et une mesure d'une seule passe
mesurerait la granularité de l'horloge plutôt que le code.

Les sommets sont générés par une progression qui les répartit sur une plage
large, pour que la division perspective ne travaille pas toujours sur la même
valeur — un diviseur constant se prêterait à une prédiction que le cas réel
n'offre pas.

La mesure est **ignorée sur l'hôte**, et le dire est le plus important de ce
document : un Ryzen moderne ne renseigne en rien sur un Pentium II à 400 MHz, et
un chiffre relevé là-bas donnerait une fausse assurance.
