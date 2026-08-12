#!/usr/bin/env bash
# Enveloppe de compatibilité vers `check_imports.py`.
#
# Ce script était l'outil de E00-S01. E01-S04 l'a remplacé par
# `tools/win95/check_imports.py`, qui distingue les DLL système des DLL de
# pilote, nomme l'objet fautif et porte une liste d'exceptions justifiées.
#
# Il subsiste parce que les documents de recherche et les ADR déjà écrits
# donnent cette ligne de commande, et surtout pour qu'il n'existe **qu'une seule
# base de référence** : l'ancienne version tenait la sienne dans
# `$DKR_WIN95_PREFIX/win95-exports.txt`, hors du dépôt. Deux bases qui divergent
# sont pires qu'une seule imparfaite — c'est ainsi qu'on finit par ne plus faire
# confiance à l'outil.
#
#   tools/win95/check-win95-imports.sh <binaire>...
#   tools/win95/check-win95-imports.sh --refresh
#
# La base versionnée vit désormais dans `tools/win95/exports/`, avec sa
# provenance.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$HERE/check_imports.py" "$@"
