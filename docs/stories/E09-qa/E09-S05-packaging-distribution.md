# E09-S05 — Empaquetage et distribution

| | |
|---|---|
| **Épic** | E09 — Intégration, QA et distribution |
| **Statut** | TODO |
| **Priorité** | P1 |
| **Estimation** | M |
| **Dépend de** | E01-S04, E06-S06, E09-S03, E09-S04 |
| **Bloque** | — |

## Contexte

Le paquet doit s'installer sur une machine de 1998, ce qui exclut les formats
modernes : pas d'archive au format récent, pas d'installeur exigeant un runtime
absent, pas de nom de fichier long si le support est en FAT16.

Trois règles du projet restent entières et priment sur toute considération de
commodité :

- **aucun asset redistribué.** Ni ROM, ni sauvegarde, ni asset extrait — le paquet
  est scanné avant distribution (`scripts/scan_for_game_assets.py`, exigé par
  `docs/ASSET_POLICY.md`) ;
- **les licences.** Le runtime hérite du GPL-3.0 de N64ModernRuntime
  (`runtime-recomp/CMakeLists.txt:57-59`) ; s'y ajoutent les conditions des
  sources Glide de 3dfx ;
- **les garde-fous** de E01-S01 et E01-S04 s'appliquent au binaire réellement
  distribué, pas seulement à celui de développement.

## Objectif

Produire un paquet installable sur Windows 95, complet, vérifié, et conforme aux
règles de distribution.

## Périmètre

**Dans :** l'empaquetage, l'installation, la documentation utilisateur, les
vérifications.

**Hors :** le contenu du jeu.

## Travail

1. Choisir le format. Une archive ZIP simple à décompresser dans un dossier est
   probablement suffisante et plus robuste qu'un installeur ; si un installeur est
   retenu, il doit fonctionner sans dépendance moderne.
2. Composer le paquet : exécutable, redistribuables décidés par E01-S03,
   configuration par défaut commentée (E06-S05), documentation, licences.
3. Traiter les DLL de Glide. Elles sont fournies par le pilote de la carte et ne
   doivent pas être redistribuées ; le paquet doit indiquer clairement ce que
   l'utilisateur doit avoir installé, et le programme le vérifier au lancement
   (E05-S01).
4. Écrire le `LISEZMOI` : configuration requise, installation, où placer sa ROM
   (E06-S06), réglages disponibles, problèmes connus, écarts de rendu assumés
   (`docs/RENDER-DIFFERENCES.md`).
5. Respecter les contraintes de nommage 8.3 partout où le support peut être en
   FAT16.
6. Automatiser la construction du paquet, sur le modèle de
   `scripts/Package-Windows.ps1` et `Package-Linux-AppImage.sh`, en y intégrant les
   contrôles bloquants : jeu d'instructions (E01-S01), imports PE (E01-S04),
   absence d'assets (`scan_for_game_assets.py`), tests (E09-S03).
7. Vérifier l'installation sur une machine émulée vierge, puis sur du matériel
   réel — décompression, lancement, première partie, sans aucun outil de
   développement présent.
8. Rassembler les licences : GPL-3.0 du runtime, conditions des sources 3dfx,
   et les mentions déjà présentes dans `THIRD_PARTY.md`.

## Critères d'acceptation

- [ ] Le paquet s'installe et se lance sur une machine Windows 95 vierge.
- [ ] Aucun asset de jeu n'est inclus, vérifié par `scan_for_game_assets.py`.
- [ ] Les licences sont complètes et exactes, sources Glide comprises.
- [ ] Les DLL Glide ne sont pas redistribuées, et leur absence est signalée
      clairement au lancement.
- [ ] Le `LISEZMOI` couvre configuration requise, installation, ROM, réglages,
      problèmes connus et écarts de rendu.
- [ ] Les noms de fichiers sont compatibles 8.3.
- [ ] La construction du paquet est automatisée avec tous les contrôles bloquants.
- [ ] L'installation est vérifiée sur émulateur **et** sur matériel réel.

## Risques

Un paquet qui suppose la présence d'un composant non redistribuable — une version
de DirectX, un runtime C, un pilote 3dfx — échouera chez une partie des
utilisateurs, sur des machines auxquelles personne n'a accès pour diagnostiquer.
La vérification sur machine vierge de l'étape 7 est ce qui attrape ces
suppositions avant qu'elles ne deviennent des rapports d'échec.

## Références

- `docs/ASSET_POLICY.md`, `scripts/scan_for_game_assets.py`
- `scripts/Package-Windows.ps1`, `scripts/Package-Linux-AppImage.sh`
- `RELEASE-VALIDATION.md`, `THIRD_PARTY.md`
- `runtime-recomp/CMakeLists.txt:57-59` — licence GPL-3.0 du runtime
