# E06-S06 — Sélection de la ROM sans lanceur

| | |
|---|---|
| **Épic** | E06 — Plateforme Windows 95 |
| **Statut** | TODO |
| **Priorité** | P2 |
| **Estimation** | S |
| **Dépend de** | E02-S04, E06-S05 |
| **Bloque** | E09-S05 |

## Contexte

DKR-R demande sa ROM au premier lancement par un lanceur SDL pilotable à la
manette. Ce lanceur disparaît avec SDL2 et ImGui, et le jeu ne peut évidemment pas
démarrer sans ROM — l'utilisateur fournit la sienne, aucun asset n'étant
redistribué (`docs/ASSET_POLICY.md`).

Il faut donc une voie simple pour la désigner, adaptée aux usages de la cible : sur
Windows 95, un jeu s'installe dans son dossier, et déposer un fichier à côté de
l'exécutable est un geste naturel.

E02-S06 a déjà mis en place le strict minimum pour démarrer. Ce ticket rend la
chose utilisable par quelqu'un d'autre que le développeur.

## Objectif

Permettre à un utilisateur de désigner sa ROM sans lanceur graphique, avec un
message clair quand elle est absente ou invalide.

## Périmètre

**Dans :** découverte de la ROM, validation, messages d'erreur.

**Hors :** le chargement lui-même (E02-S04).

## Travail

1. Implémenter la recherche par ordre de priorité : argument de ligne de commande,
   puis chemin du fichier de configuration (E06-S05), puis recherche automatique
   dans le dossier de l'exécutable.
2. Implémenter la recherche automatique : parcourir le dossier à la recherche d'un
   fichier `.z64`, `.n64` ou `.v64` dont l'empreinte correspond. C'est le chemin
   qui rend l'installation évidente — déposer sa ROM à côté du jeu et lancer.
3. Enregistrer le chemin validé dans la configuration, pour éviter de revalider à
   chaque lancement. La validation SHA-1 de 12 Mo n'est pas gratuite sur un
   Pentium II (E02-S04).
4. Écrire les messages d'erreur pour chaque cas : ROM absente, ROM illisible,
   mauvaise version, empreinte incorrecte. Chacun doit indiquer quoi faire, pas
   seulement ce qui ne va pas.
5. Afficher ces messages de façon visible sur la cible : boîte de message Win32,
   puisqu'il n'y a pas de console — plus la trace dans le journal.
6. Documenter la procédure dans le fichier `LISEZMOI` du paquet de distribution
   (E09-S05), avec l'empreinte attendue.
7. Vérifier que le message reste correct sur une ROM de la bonne version mais dans
   un ordre d'octets différent — le runtime normalise avant de calculer
   l'empreinte, et ce cas ne doit pas être rejeté à tort.

## Critères d'acceptation

- [ ] Les trois voies de découverte fonctionnent, dans l'ordre de priorité.
- [ ] Une ROM déposée à côté de l'exécutable est trouvée automatiquement.
- [ ] Le chemin validé est mémorisé et la revalidation évitée.
- [ ] Chaque cas d'erreur produit un message qui indique quoi faire.
- [ ] Les messages sont visibles sans console, et tracés dans le journal.
- [ ] Les trois ordres d'octets sont acceptés.
- [ ] La procédure est documentée dans le paquet de distribution.

## Risques

C'est le premier contact de l'utilisateur avec le portage. Un message d'erreur
obscur à cette étape, sur une machine sans console et sans outils, se solde par un
abandon. Le soin apporté aux messages n'est pas cosmétique.

## Références

- `docs/ROM_SETUP.md`, `docs/ASSET_POLICY.md`
- `README.md` — empreinte SHA-1 attendue après normalisation
- E02-S04 — chargement et validation
