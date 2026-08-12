#!/usr/bin/env bash
# E00-S01 — compile les sondes et rapporte, pour chacune, combien de symboles
# Windows 95 ne peut pas fournir.
#
#   tools/win95/probes/build-probes.sh              (modele de threads win32)
#   tools/win95/probes/build-probes.sh posix        (modele winpthreads)
#
# Chaque sonde n'utilise qu'une facilite de la bibliotheque standard. La
# comparaison des tables d'imports attribue chaque bloquant a la facilite qui
# le tire, ce qu'un decompte d'API sur les sources ne saurait faire : le code
# du projet n'appelle aucune de ces fonctions, c'est libstdc++ qui les reclame.
#
# `-static` n'est pas cosmetique : sans lui le binaire depend de
# LIBGCC_S_DW2-1.DLL, absente de Windows 95, et refuse de se charger.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
export PATH="$PREFIX/opt/mingw/usr/bin:$PATH"

MODEL="${1:-win32}"
CXX="i686-w64-mingw32-g++-$MODEL"
OUT="${DKR_PROBE_OUT:-$ROOT/build/win95-probes/$MODEL}"

command -v "$CXX" >/dev/null || {
  echo "erreur: $CXX introuvable." >&2
  echo "  modele win32 : g++-mingw-w64-i686-win32" >&2
  echo "  modele posix : g++-mingw-w64-i686-posix" >&2
  exit 1
}

mkdir -p "$OUT"
printf '%-12s %9s %10s   %s\n' sonde imports bloquants "symboles absents"
printf '%-12s %9s %10s   %s\n' ------------ --------- ---------- ----------------

for src in "$HERE"/p_*.cpp; do
  name="$(basename "$src" .cpp)"
  "$CXX" -std=c++20 -O2 -march=pentium2 -mno-sse \
         -static -static-libgcc -static-libstdc++ \
         "$src" -o "$OUT/$name.exe" 2>/dev/null || {
    printf '%-12s %9s %10s   %s\n' "${name#p_}" - - "COMPILATION ECHOUEE"; continue; }

  # Le controleur colorise sa sortie ; on la depouille avant de la relire.
  report="$("$ROOT/tools/win95/check-win95-imports.sh" "$OUT/$name.exe" 2>/dev/null \
            | sed -E 's/\x1b\[[0-9;]*m//g' || true)"
  n=$(sed -n '1s/.*— ([0-9]+) symboles.*/\1/p' -E <<< "$report")
  miss=$(awk '/^ *ABSENT/ {print $2}' <<< "$report" | sort | tr '\n' ' ')
  nb=$(wc -w <<< "$miss")
  printf '%-12s %9s %10s   %s\n' "${name#p_}" "${n:-?}" "$nb" "${miss:-—}"
done

echo
echo "binaires dans $OUT — a executer sur la machine de test via"
echo "  scripts/Push-To-Win95-VM.sh $OUT/*.exe"
