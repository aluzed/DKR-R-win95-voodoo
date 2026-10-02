#!/usr/bin/env bash
# E09-S05 - builds the Windows 95 package: a ZIP to unpack into a folder.
#
#   scripts/Package-Win95.sh                 package build/win95/bin/DKRR.EXE
#   scripts/Package-Win95.sh --build         run Build-Win95.sh first
#
# Writes dist/win95/DKRR-W95.ZIP and leaves its contents in dist/win95/DKRR/.
#
# **Every check blocks.** A package that fails one is not written:
#
#   - the executable's instruction set (E01-S01): nothing past the Pentium II;
#   - its imports (E01-S04): every symbol Windows 95 must resolve at load time,
#     GLIDE2X.DLL reported as the driver's and not shipped;
#   - no game asset in the package (`scan_for_game_assets.py`, ASSET_POLICY.md);
#   - every name 8.3, since the package may be unpacked onto FAT16;
#   - the Windows 95 platform tests and the runtime's portable-logic suites,
#     unless --no-tests.
#
# Text files are written with CRLF line ends: NOTEPAD on Windows 95 shows a
# file with bare LFs as one line.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXE="$ROOT/build/win95/bin/DKRR.EXE"
OUT="$ROOT/dist/win95"
STAGE="$OUT/DKRR"
ZIP="$OUT/DKRR-W95.ZIP"

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

run_tests=1
for arg in "$@"; do
  case "$arg" in
    --build)    "$ROOT/Build-Win95.sh" ;;
    --no-tests) run_tests=0 ;;
    *) fail "unknown option $arg" ;;
  esac
done
[[ -f "$EXE" ]] || fail "$EXE is absent: run Build-Win95.sh, or pass --build"

say "staging $STAGE"
rm -rf "$STAGE" "$ZIP"
mkdir -p "$STAGE"
# Without its symbols: they are for profiling (`tools/win95/sampler_report.py`
# reads them from the build's copy), and a player has no use for 6 MB of them.
"${STRIP:-$HOME/.local/dkr-win95/bin/i686-w64-mingw32-strip}" -o "$STAGE/DKRR.EXE" "$EXE"

# The checks are about the binary that is shipped. The instruction-set check
# needs the symbols -- without them objdump cannot resynchronise at each
# function and decodes the data kept in the code section as SSE, seven false
# instructions on 1 October 2026 -- so it runs on the build's copy, and the
# shipped code is proved to be the same bytes: stripping removes symbols, not
# code.
say "checking the executable's instruction set"
"$ROOT/tools/win95/check-instruction-set.sh" "$EXE" >/dev/null \
  || fail "DKRR.EXE uses instructions a Pentium II does not have"
OBJCOPY="${OBJCOPY:-$HOME/.local/dkr-win95/opt/mingw/usr/bin/i686-w64-mingw32-objcopy}"
[[ -x "$OBJCOPY" ]] || OBJCOPY="$(command -v i686-w64-mingw32-objcopy || true)"
[[ -n "$OBJCOPY" ]] || fail "i686-w64-mingw32-objcopy is needed to compare the code"
"$OBJCOPY" -O binary --only-section=.text "$EXE" "$OUT/text.built"
"$OBJCOPY" -O binary --only-section=.text "$STAGE/DKRR.EXE" "$OUT/text.shipped"
cmp -s "$OUT/text.built" "$OUT/text.shipped" \
  || fail "the shipped DKRR.EXE's code differs from the checked one"
rm -f "$OUT/text.built" "$OUT/text.shipped"

say "checking its imports against Windows 95's exports"
python3 "$ROOT/tools/win95/check_imports.py" "$STAGE/DKRR.EXE" >/dev/null \
  || fail "DKRR.EXE imports something Windows 95 does not export"

if (( run_tests )); then
  say "running the Windows 95 platform tests"
  bash "$ROOT/platform/win95/tests/run-tests.sh" >/dev/null 2>&1 \
    || fail "the platform tests fail"
  say "running the runtime's portable-logic suites"
  bash "$ROOT/tools/tests/run-portable-tests.sh" >/dev/null 2>&1 \
    || fail "the portable-logic suites fail (tools/tests/run-portable-tests.sh)"
fi

crlf() { sed 's/\r$//; s/$/\r/' "$1" > "$2"; }
crlf "$ROOT/packaging/win95/README.TXT"           "$STAGE/README.TXT"
crlf "$ROOT/docs/CONFIGURATION.md"                "$STAGE/CONFIG.TXT"
crlf "$ROOT/LICENSE.md"                           "$STAGE/LICENSE.TXT"
crlf "$ROOT/extern/n64-modern-runtime/COPYING"    "$STAGE/COPYING.TXT"
crlf "$ROOT/THIRD_PARTY.md"                       "$STAGE/THIRDPTY.TXT"

say "checking that no game asset is in the package"
python3 "$ROOT/scripts/scan_for_game_assets.py" "$STAGE" --allow DKRR.EXE >/dev/null \
  || fail "the package holds something that looks like game content"
if find "$STAGE" -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' \
     -o -iname 'glide2x.dll' | grep -q .; then
  fail "a ROM or the 3dfx driver's GLIDE2X.DLL is in the package"
fi

say "checking 8.3 names"
while IFS= read -r name; do
  base="${name%.*}"; ext=""; [[ "$name" == *.* ]] && ext="${name##*.}"
  if (( ${#base} > 8 || ${#ext} > 3 )) || [[ "$base" == *.* ]] \
     || [[ "$name" =~ [^A-Z0-9._-] ]]; then
    fail "$name is not an 8.3 upper-case name"
  fi
done < <(cd "$STAGE" && find . -type f -printf '%f\n')

say "writing $ZIP"
(cd "$STAGE" && zip -X -q -9 "$ZIP" ./*)
ls -l "$ZIP"
(cd "$STAGE" && sha256sum ./* | sed 's#\./##') > "$OUT/SHA256SUMS"
say "done: $(unzip -l "$ZIP" | tail -1)"
