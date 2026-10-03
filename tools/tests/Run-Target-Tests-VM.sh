#!/usr/bin/env bash
# E09-S03 - runs the target's test executables on the test machine and reports
# each one's result on the host:
#
#   - the platform tests E02 asks for: THREADS.EXE (E02-S01), CLOCKT.EXE
#     (E02-S03), FILEIOT.EXE, SAVEMGR.EXE and SAVECDC.EXE (E02-S05);
#   - the runtime's portable-logic suites, `build/win95/bin/portable/PT*.EXE`,
#     built from tools/tests/portable-suites.txt.
#
#   tools/tests/Run-Target-Tests-VM.sh
#
# The executables go to D:\PT with a batch file that runs them one after the
# other, each one's output to D:\PT\<name>.OUT and its verdict -- the exit code,
# which COMMAND.COM sees as ERRORLEVEL -- to D:\PTALL.TXT, ended by a line
# `END`. The script waits for that line, so a suite that hangs shows as a
# missing verdict rather than as a pass.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
prefix="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
vm="${DKR_WIN95_VM:-dkr-p2-voodoo2}"
image="$prefix/vm/$vm/transfer.img@@32256"
drive="$ROOT/scripts/Drive-Win95-VM.sh"
bin="$ROOT/build/win95/bin/portable"
export MTOOLS_SKIP_CHECK=1
say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

shopt -s nullglob
# COMMAND.COM cannot stop a program that hangs, and on 3 October 2026
# SAVEMGR.EXE did, keeping every suite after it from running. It goes last, so
# that a hang there costs only its own verdict.
exes=()
for name in THREADS CLOCKT FILEIOT SAVECDC; do
  [[ -f "$ROOT/build/win95/bin/$name.EXE" ]] || fail "$name.EXE is absent: run Build-Win95.sh"
  exes+=("$ROOT/build/win95/bin/$name.EXE")
done
portable=("$bin"/PT*.EXE)
(( ${#portable[@]} > 0 )) || fail "no PT*.EXE in $bin: run Build-Win95.sh"
exes+=("${portable[@]}")
[[ -f "$ROOT/build/win95/bin/SAVEMGR.EXE" ]] || fail "SAVEMGR.EXE is absent: run Build-Win95.sh"
exes+=("$ROOT/build/win95/bin/SAVEMGR.EXE")

work="$(mktemp -d)"; trap 'rm -rf -- "$work"' EXIT
{
  printf '@ECHO OFF\r\n'
  # Run from D:\PT, so that the suites that write files write them there.
  printf 'D:\r\n'
  printf 'CD \\PT\r\n'
  printf 'ECHO PTALL> D:\\PTALL.TXT\r\n'
  # Labels by number: COMMAND.COM reads only a label's first eight
  # characters, and F_PTPRES and F_PTPRESID would be the same label.
  i=0
  for exe in "${exes[@]}"; do
    i=$((i + 1)); name="$(basename "$exe" .EXE)"
    printf 'D:\\PT\\%s.EXE > D:\\PT\\%s.OUT\r\n' "$name" "$name"
    printf 'IF ERRORLEVEL 1 GOTO F%02d\r\n' "$i"
    printf 'ECHO ok %s>> D:\\PTALL.TXT\r\n' "$name"
    printf 'GOTO N%02d\r\n' "$i"
    printf ':F%02d\r\n' "$i"
    printf 'ECHO FAIL %s>> D:\\PTALL.TXT\r\n' "$name"
    printf ':N%02d\r\n' "$i"
  done
  printf 'ECHO END>> D:\\PTALL.TXT\r\n'
} > "$work/PTALL.BAT"

say "copying ${#exes[@]} suites to D:\\PT"
"$drive" stop >/dev/null 2>&1 || true
"$prefix/bin/mmd" -i "$image" ::/PT 2>/dev/null || true
"$prefix/bin/mdel" -i "$image" "::/PT/*.*" ::/PTALL.TXT 2>/dev/null || true
"$prefix/bin/mcopy" -o -i "$image" "${exes[@]}" ::/PT/
"$prefix/bin/mcopy" -o -i "$image" "$work/PTALL.BAT" ::/PTALL.BAT

say "booting"
"$drive" boot >/dev/null
"$drive" run 'D:\PTALL.BAT' >/dev/null

say "waiting for the verdicts"
deadline=$(( $(date +%s) + ${DKR_PT_TIMEOUT:-900} ))
while (( $(date +%s) < deadline )); do
  sleep 20
  # The guest's writes reach the image when it flushes; reading it while the
  # machine runs is safe for a file the guest has finished with.
  if "$prefix/bin/mtype" -i "$image" ::/PTALL.TXT 2>/dev/null | tr -d '\r' | grep -qx END; then
    break
  fi
done
sleep 10
"$drive" stop >/dev/null 2>&1 || true
"$prefix/bin/mcopy" -o -i "$image" ::/PTALL.TXT "$work/PTALL.TXT" 2>/dev/null \
  || fail "no D:\\PTALL.TXT: the batch file did not run"
tr -d '\r' < "$work/PTALL.TXT" | sed '1d'
grep -qx END <(tr -d '\r' < "$work/PTALL.TXT") || fail "the run did not finish: a suite hung"
failed=$(tr -d '\r' < "$work/PTALL.TXT" | grep -c '^FAIL' || true)
passed=$(tr -d '\r' < "$work/PTALL.TXT" | grep -c '^ok' || true)
echo
echo "$passed passed, $failed failed, on the test machine"
for f in $(tr -d '\r' < "$work/PTALL.TXT" | awk '/^FAIL/ {print $2}'); do
  echo "--- $f"
  "$prefix/bin/mtype" -i "$image" "::/PT/$f.OUT" 2>/dev/null | tr -d '\r' | tail -8
done
(( failed == 0 ))
