#!/usr/bin/env bash
# E09-S01 / E00-S02 - Builds the Glide demonstration for Windows 95.
#
# With no C library: mingw-w64's CRT startup is what causes trouble under
# Windows 95, and leaving it out gives an executable that depends only on
# kernel32, user32 and glide2x.dll. The resulting binary therefore doubles as a
# witness for E00-S02.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:-$HERE/GLIDETST.EXE}"
CC="$PREFIX/bin/i686-w64-mingw32-gcc"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

[[ -x "$CC" ]] || die "i686-w64-mingw32-gcc missing under $PREFIX/bin"

say "Compiling without the CRT, targeting a Pentium II with no SSE"
"$CC" -O2 -Wall -Wextra \
  -march=pentium2 -mtune=pentium3 -mfpmath=387 -mno-sse -mno-sse2 \
  -nostdlib -nostartfiles -nodefaultlibs \
  -Wl,--subsystem,windows -Wl,-e,_start \
  -o "$OUT" "$HERE/glidetest.c" \
  -lkernel32 -luser32

say "Check: no SSE instruction"
if "$PREFIX/bin/i686-w64-mingw32-objdump" -d "$OUT" \
   | grep -qE '\b(movss|movsd|cvtsi2s[sd]|cvttss2si|pxor|movaps|xorps|movdqa)\b'; then
  die "SSE instructions remain in $OUT"
fi
echo "  none"

say "Check: imported DLLs"
"$PREFIX/bin/i686-w64-mingw32-objdump" -p "$OUT" | grep -i 'DLL Name' | sed 's/^/  /'

say "Check: imported symbols"
"$PREFIX/bin/i686-w64-mingw32-objdump" -p "$OUT" \
  | awk '/DLL Name/{d=$3} /^\t[0-9]+\t/{print "  " d ": " $NF}' | sort -u | sed 's/^/  /' | head -20

printf '\n\033[1;34m==>\033[0m %s\n' "$OUT ($(stat -c%s "$OUT") bytes)"
