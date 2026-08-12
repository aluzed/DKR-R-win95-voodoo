#!/usr/bin/env bash
# E01-S03 — tests de la couche plate-forme qui s'executent sur l'hote.
#
# Le rebouclage de GetTickCount se produit apres 49,7 jours. Attendre n'est pas
# un protocole de test : la logique d'accumulation est une fonction pure, sans
# dependance a Windows, et se pilote donc ici avec des valeurs choisies — avec
# le compilateur de l'hote, pas celui de la cible.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CC="${CC:-cc}"
command -v "$CC" >/dev/null || { echo "erreur: aucun compilateur hote ($CC)" >&2; exit 2; }

tmp="$(mktemp -d)"; trap 'rm -rf -- "$tmp"' EXIT
"$CC" -O2 -Wall -Wextra -o "$tmp/test_tick64" \
      "$HERE/test_tick64.c" "$HERE/../tick64.c"
"$tmp/test_tick64"
