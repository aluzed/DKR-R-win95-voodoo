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
  all|tick64|threading|clock|fileio|saves|render) ;;
  *) echo "usage: $0 [all|tick64|threading|clock|fileio|saves|render]" >&2; exit 2 ;;
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
  # Le point d'indirection est eprouve **deux fois** sur l'hote, et c'est le
  # deuxieme montage qui compte le plus.
  #
  #   sans DKR_TARGET_WIN95 : la branche std::, celle des cibles modernes
  #   avec  DKR_TARGET_WIN95 : la branche Windows 95, sur la dorsale POSIX
  #
  # Le second n'est pas la cible reelle — la dorsale POSIX est un vehicule — mais
  # il execute la *logique* de la branche Windows 95 : les valeurs de retour, les
  # conversions, l'ordre des appels. C'est la qu'un ecart de contrat se voit, et
  # sans attendre un aller-retour de vingt minutes vers la machine emulee.
  #
  # Les deux defauts trouves en etendant la couche auraient ete pris ici : le
  # `create_directories` qui rendait true sur un repertoire present, et le
  # `weakly_canonical` qui ne resolvait pas « .. ». Le second l'a effectivement
  # ete, des la premiere execution de ce montage.
  "$CXX" -O2 -Wall -Wextra -I"$HERE/.." -o "$tmp/test_fileio_seam" \
         "$HERE/test_fileio_seam.cpp" "$HERE/../fileio.cpp"
  "$CXX" -O2 -Wall -Wextra -DDKR_TARGET_WIN95 -I"$HERE/.." \
         -o "$tmp/test_fileio_seam95" \
         "$HERE/test_fileio_seam.cpp" "$HERE/../fileio.cpp"
  # Les signatures employees par les quatre fichiers que RT64 seul construit, et
  # qui ne peuvent donc pas etre compiles ici. Rien ne s'execute : c'est la
  # compilation, dans les deux branches, qui est le controle.
  "$CXX" -std=c++20 -Wall -Wextra -fsyntax-only -I"$HERE/.." \
         "$HERE/test_fileio_signatures.cpp"
  "$CXX" -std=c++20 -Wall -Wextra -fsyntax-only -DDKR_TARGET_WIN95 -I"$HERE/.." \
         "$HERE/test_fileio_signatures.cpp"
  echo "  ok    les signatures du jeu compilent dans les deux branches"

  echo
  # Les fichiers d'essai sont crees dans le repertoire courant : on l'isole.
  ( cd "$tmp" && "$tmp/test_fileio" )
  echo "  -- point d'indirection, branche des cibles modernes --"
  ( cd "$tmp" && "$tmp/test_fileio_seam" )
  echo "  -- point d'indirection, branche Windows 95 sur dorsale POSIX --"
  ( cd "$tmp" && "$tmp/test_fileio_seam95" )
fi

# --- E02-S05 : les suites de sauvegarde, sur l'hote ---------------------------
#
# Elles sont compilees pour la cible et executees sur la machine — c'est la que
# se ferment les criteres d'acceptation — mais les faire tourner ici aussi
# raccourcit le cycle de plusieurs minutes a une seconde, et prend les ecarts de
# contrat avant l'aller-retour.
#
# `save_manager` est construit **deux fois**, comme le point d'indirection : la
# branche des cibles modernes, puis la branche Windows 95 sur les dorsales
# POSIX. C'est le second montage qui a pris le create_directories rendant true
# sur un repertoire present.
if [[ "$suite" == "all" || "$suite" == "saves" ]]; then
  command -v "$CXX" >/dev/null \
    || { echo "erreur: aucun compilateur C++ hote ($CXX)" >&2; exit 2; }
  GAME="$HERE/../../../runtime-recomp/src/game"
  TESTS="$HERE/../../../runtime-recomp/tests"
  echo
  "$CXX" -O2 -Wall -Wextra -std=c++20 -I"$GAME" -I"$HERE/../.." -I"$HERE/.." \
         -o "$tmp/save_codec" "$TESTS/dkr_save_codec_tests.cpp" "$GAME/dkr_save_codec.cpp"
  "$tmp/save_codec"
  for mode in "" "-DDKR_TARGET_WIN95=1"; do
    label="branche des cibles modernes"
    [[ -n "$mode" ]] && label="branche Windows 95 sur dorsales POSIX"
    "$CXX" -O2 -Wall -Wextra -std=c++20 $mode -I"$GAME" -I"$HERE/../.." -I"$HERE/.." \
           -o "$tmp/save_manager" \
           "$TESTS/save_manager_tests.cpp" "$GAME/save_manager.cpp" \
           "$GAME/dkr_save_codec.cpp" "$HERE/../fileio.cpp" "$HERE/../threading.cpp" \
           -lpthread
    echo "  -- save_manager, $label --"
    ( cd "$tmp" && "$tmp/save_manager" )
  done
fi

# --- E04-S08 : le rasteriseur logiciel de reference ---------------------------
#
# L'oracle du rendu. Chaque controle compare un pixel relu a une valeur calculee
# analytiquement, parce qu'un oracle qu'on verifie a l'oeil n'est pas un oracle :
# sa valeur entiere tient dans la confiance qu'on lui accorde, et « ca a l'air
# juste » ne se transmet pas.
#
# Le meme binaire construit pour la cible tourne sur la machine, et les deux
# rendent les memes octets.
if [[ "$suite" == "all" || "$suite" == "render" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "erreur: aucun compilateur C hote ($CC)" >&2; exit 2; }
  R="$HERE/../../render"
  "$CC" -std=gnu11 -O2 -Wall -Wextra -I"$HERE/../.." -I"$R" \
        -o "$tmp/test_software" "$R/tests/test_software.c" "$R/software.c"
  echo
  ( cd "$tmp" && "$tmp/test_software" )
fi
