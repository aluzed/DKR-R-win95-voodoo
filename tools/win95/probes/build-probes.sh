#!/usr/bin/env bash
# E00-S01 - compiles the probes and reports, for each, how many symbols
# Windows 95 cannot supply.
#
#   tools/win95/probes/build-probes.sh              (win32 threading model)
#   tools/win95/probes/build-probes.sh posix        (winpthreads model)
#
# Each probe uses exactly one standard-library facility. Comparing the import
# tables attributes every blocker to the facility that pulls it in, which an API
# count over the sources could not do: the project's code calls none of these
# functions, it is libstdc++ that asks for them.
#
# `-static` is not cosmetic: without it the binary depends on
# LIBGCC_S_DW2-1.DLL, absent from Windows 95, and refuses to load.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
export PATH="$PREFIX/opt/mingw/usr/bin:$PATH"

MODEL="${1:-win32}"
CXX="i686-w64-mingw32-g++-$MODEL"
OUT="${DKR_PROBE_OUT:-$ROOT/build/win95-probes/$MODEL}"

command -v "$CXX" >/dev/null || {
  echo "error: $CXX not found." >&2
  echo "  win32 model: g++-mingw-w64-i686-win32" >&2
  echo "  posix model: g++-mingw-w64-i686-posix" >&2
  exit 1
}

mkdir -p "$OUT"
printf '%-12s %9s %10s   %s\n' probe imports blockers "missing symbols"
printf '%-12s %9s %10s   %s\n' ------------ --------- ---------- ----------------

for src in "$HERE"/p_*.cpp; do
  name="$(basename "$src" .cpp)"
  "$CXX" -std=c++20 -O2 -march=pentium2 -mno-sse \
         -static -static-libgcc -static-libstdc++ \
         "$src" -o "$OUT/$name.exe" 2>/dev/null || {
    printf '%-12s %9s %10s   %s\n' "${name#p_}" - - "COMPILATION FAILED"; continue; }

  # The checker colours its output; we strip it before reading it back.
  report="$("$ROOT/tools/win95/check-win95-imports.sh" "$OUT/$name.exe" 2>/dev/null \
            | sed -E 's/\x1b\[[0-9;]*m//g' || true)"
  n=$(sed -n '1s/.*- ([0-9]+) symbols.*/\1/p' -E <<< "$report")
  miss=$(awk '/^ *MISSING/ {print $2}' <<< "$report" | sort | tr '\n' ' ')
  nb=$(wc -w <<< "$miss")
  printf '%-12s %9s %10s   %s\n' "${name#p_}" "${n:-?}" "$nb" "${miss:--}"
done

echo
echo "binaries in $OUT - to be run on the test machine via"
echo "  scripts/Push-To-Win95-VM.sh $OUT/*.exe"
