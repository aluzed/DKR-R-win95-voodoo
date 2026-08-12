#!/usr/bin/env bash
# E09-S01 / E00-S02 — Construit la démonstration Glide pour Windows 95.
#
# Sans bibliothèque C : le démarrage du CRT de mingw-w64 est ce qui pose
# problème sous Windows 95, et l'écarter donne un exécutable qui ne dépend que
# de kernel32, user32 et glide2x.dll. Le binaire produit sert donc aussi de
# témoin pour E00-S02.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:-$HERE/GLIDETST.EXE}"
CC="$PREFIX/bin/i686-w64-mingw32-gcc"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merreur:\033[0m %s\n' "$*" >&2; exit 1; }

[[ -x "$CC" ]] || die "i686-w64-mingw32-gcc absent sous $PREFIX/bin"

say "Compilation sans CRT, cible Pentium II sans SSE"
"$CC" -O2 -Wall -Wextra \
  -march=pentium2 -mtune=pentium3 -mfpmath=387 -mno-sse -mno-sse2 \
  -nostdlib -nostartfiles -nodefaultlibs \
  -Wl,--subsystem,windows -Wl,-e,_start \
  -o "$OUT" "$HERE/glidetest.c" \
  -lkernel32 -luser32

say "Contrôle : aucune instruction SSE"
if "$PREFIX/bin/i686-w64-mingw32-objdump" -d "$OUT" \
   | grep -qE '\b(movss|movsd|cvtsi2s[sd]|cvttss2si|pxor|movaps|xorps|movdqa)\b'; then
  die "des instructions SSE subsistent dans $OUT"
fi
echo "  aucune"

say "Contrôle : DLL importées"
"$PREFIX/bin/i686-w64-mingw32-objdump" -p "$OUT" | grep -i 'DLL Name' | sed 's/^/  /'

say "Contrôle : symboles importés"
"$PREFIX/bin/i686-w64-mingw32-objdump" -p "$OUT" \
  | awk '/DLL Name/{d=$3} /^\t[0-9]+\t/{print "  " d ": " $NF}' | sort -u | sed 's/^/  /' | head -20

printf '\n\033[1;34m==>\033[0m %s\n' "$OUT ($(stat -c%s "$OUT") octets)"
