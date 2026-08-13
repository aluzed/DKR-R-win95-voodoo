#!/usr/bin/env bash
# Tests de la couche plate-forme qui s'executent sur l'hote.
#
#   platform/win95/tests/run-tests.sh                  les deux suites
#   platform/win95/tests/run-tests.sh tick64           une seule
#   platform/win95/tests/run-tests.sh threading
#   DKR_STRESS_SECONDS=600 platform/win95/tests/run-tests.sh threading
#
# Deux suites, pour deux raisons differentes :
#
#   tick64     (E01-S03) le rebouclage de GetTickCount se produit apres 49,7
#              jours. Attendre n'est pas un protocole : la logique est une
#              fonction pure, pilotee ici avec des valeurs choisies.
#
#   clock      (E02-S03) la base de temps. Sa partie delicate — la conversion
#              vers le compteur du VR4300 et le rebouclage 32 bits — est faite
#              de fonctions pures, donc entierement pilotable ici.
#
#   threading  (E02-S01) la meme source que THREADS.EXE, qui tourne sous
#              Windows 95 emule. Ici elle s'appuie sur le vehicule POSIX de
#              threading.cpp, ce qui rend le cycle de mise au point court.
#              Passer ici ne prouve rien de la cible — c'est pourquoi le meme
#              binaire est aussi execute sur la machine.
#
# CTest les enregistre separement, de sorte que `-R` puisse en viser une.
set -euo pipefail

suite="${1:-all}"
case "$suite" in
  all|tick64|threading|clock|fileio) ;;
  *) echo "usage: $0 [all|tick64|threading|clock|fileio]" >&2; exit 2 ;;
esac

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CC="${CC:-cc}"
CXX="${CXX:-c++}"

tmp="$(mktemp -d)"; trap 'rm -rf -- "$tmp"' EXIT

# --- E01-S03 : rebouclage de l'horloge ---------------------------------------

if [[ "$suite" == "all" || "$suite" == "tick64" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "erreur: aucun compilateur C hote ($CC)" >&2; exit 2; }
  "$CC" -O2 -Wall -Wextra -o "$tmp/test_tick64" \
        "$HERE/test_tick64.c" "$HERE/../tick64.c"
  "$tmp/test_tick64"
fi

# --- E02-S01 : fils et synchronisation ---------------------------------------

if [[ "$suite" == "all" || "$suite" == "threading" ]]; then
  command -v "$CXX" >/dev/null \
    || { echo "erreur: aucun compilateur C++ hote ($CXX)" >&2; exit 2; }

  # `timeout` n'est pas un confort mais le detecteur d'interblocage : un reveil
  # perdu ne produit pas un mauvais resultat, il produit une attente qui ne finit
  # pas. Verifie en injectant la perte d'un reveil sur mille, qui fait bien
  # expirer ce delai. Sans lui, la suite resterait suspendue au lieu d'echouer.
  # Sur macOS il vient des coreutils, sous le nom `gtimeout`.
  TIMEOUT="$(command -v timeout || command -v gtimeout || true)"
  [[ -n "$TIMEOUT" ]] || {
    echo "erreur: 'timeout' est absent — c'est lui qui transforme un interblocage" >&2
    echo "        en echec. Sur macOS : brew install coreutils (gtimeout)." >&2
    exit 2
  }

  "$CXX" -O2 -Wall -Wextra -o "$tmp/test_threading" \
         "$HERE/test_threading.cpp" "$HERE/../threading.cpp" -lpthread

  stress_seconds="${DKR_STRESS_SECONDS:-0}"
  args=()
  limit=120
  if [[ "$stress_seconds" -gt 0 ]]; then
    args=(--stress "$stress_seconds")
    limit=$(( stress_seconds + 120 ))   # de quoi finir le tour en cours
  fi

  echo
  # `${args[@]+...}` et non `"${args[@]}"` : sur un tableau vide, le second
  # est une variable non liee pour bash 3.2, celui que macOS livre encore.
  "$TIMEOUT" "${limit}s" "$tmp/test_threading" ${args[@]+"${args[@]}"} || {
    rc=$?
    [[ $rc -eq 124 ]] && echo "ECHEC : delai expire — interblocage ou reveil perdu" >&2
    exit $rc
  }
fi

# --- E02-S03 : base de temps -------------------------------------------------

if [[ "$suite" == "all" || "$suite" == "clock" ]]; then
  command -v "$CXX" >/dev/null \
    || { echo "erreur: aucun compilateur C++ hote ($CXX)" >&2; exit 2; }
  "$CXX" -O2 -Wall -Wextra -o "$tmp/test_clock" \
         "$HERE/test_clock.cpp" "$HERE/../clock.cpp" "$HERE/../tick64.c"
  echo
  "$tmp/test_clock"
fi

# --- E02-S05 : ecriture durable ----------------------------------------------

if [[ "$suite" == "all" || "$suite" == "fileio" ]]; then
  command -v "$CXX" >/dev/null \
    || { echo "erreur: aucun compilateur C++ hote ($CXX)" >&2; exit 2; }
  "$CXX" -O2 -Wall -Wextra -o "$tmp/test_fileio" \
         "$HERE/test_fileio.cpp" "$HERE/../fileio.cpp"
  "$CXX" -O2 -Wall -Wextra -I"$HERE/.." -o "$tmp/test_fileio_seam" \
         "$HERE/test_fileio_seam.cpp" "$HERE/../fileio.cpp"
  echo
  # Les fichiers d'essai sont crees dans le repertoire courant : on l'isole.
  ( cd "$tmp" && "$tmp/test_fileio" )
  ( cd "$tmp" && "$tmp/test_fileio_seam" )
fi
