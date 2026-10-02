#!/usr/bin/env bash
# E09-S03 - the runtime's portable-logic suites, built and run on the host
# without the modern build's dependencies.
#
#   tools/tests/run-portable-tests.sh            every suite below
#   tools/tests/run-portable-tests.sh SaveCodec  the suites whose name matches
#
# **Why this exists.** `runtime-recomp/CMakeLists.txt` declares seventy suites,
# run by CTest in `Build-Linux.sh` -- but configuring that build needs GekkoNet,
# SDL3, RT64 and the generated sources, none of which a Windows 95 checkout has.
# The suites below are the ones whose code under test is compiled into the
# Windows 95 game (`DKR_WIN95_GAME_SOURCES`) or included by it, and which need
# nothing else: thirty of the seventy, sorted on 2 October 2026 by following
# each test's includes into `src/game` (see `docs/TESTING.md`). They run here in
# about a minute and the package script runs them before writing a package.
#
# Two shared suites are not here: `DKRRomRevision` needs three ROM revisions
# as arguments, and `DKROnlineInputBroker` links the netplay library. The other
# thirty-eight test code the Windows 95 build does not contain.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
filter="${1:-}"
T="$ROOT/runtime-recomp/tests"
G="$ROOT/runtime-recomp/src/game"

# The list is shared with the target's build: tools/tests/portable-suites.txt.
LIST="$ROOT/tools/tests/portable-suites.txt"
[[ -f "$LIST" ]] || { echo "error: $LIST is missing" >&2; exit 2; }

INCLUDES=(
  -I "$G" -I "$ROOT/runtime-recomp/src" -I "$ROOT/platform"
  -I "$ROOT/extern/n64-modern-runtime/thirdparty"
  -I "$ROOT/extern/n64-modern-runtime/ultramodern/include"
  -I "$ROOT/extern/n64-modern-runtime/librecomp/include"
  -I "$ROOT/extern/n64-modern-runtime/N64Recomp/include"
)

tmp="$(mktemp -d)"; trap 'rm -rf -- "$tmp"' EXIT
passed=0; failed=0; ran=0
while read -r short name test sources; do
  [[ -z "$short" || "$short" == \#* ]] && continue
  [[ -n "$filter" && "$name" != *"$filter"* ]] && continue
  ran=$((ran + 1))
  files=("$T/$test")
  # shellcheck disable=SC2086
  for s in $sources; do files+=("$G/$s"); done
  # -w: these are the modern build's sources, with its own warning policy;
  # this script answers whether they behave, not how they compile.
  if ! "$CXX" -std=c++20 -O1 -w "${INCLUDES[@]}" -o "$tmp/$name" "${files[@]}" \
       >"$tmp/$name.log" 2>&1; then
    echo "  FAIL  $name: does not build"; sed -n '1,5p' "$tmp/$name.log"
    failed=$((failed + 1)); continue
  fi
  if timeout 120 "$tmp/$name" >"$tmp/$name.out" 2>&1; then
    echo "  ok    $name"; passed=$((passed + 1))
  else
    echo "  FAIL  $name (exit $?)"; tail -5 "$tmp/$name.out"
    failed=$((failed + 1))
  fi
done < "$LIST"
(( ran > 0 )) || { echo "no suite matches '$filter'" >&2; exit 2; }
echo
echo "$passed passed, $failed failed"
(( failed == 0 ))
