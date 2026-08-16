#!/usr/bin/env bash
# E00-S02 - builds the T1/T2/T3 witnesses with each candidate toolchain, and
# reports for each: size, imported DLLs and symbols, presence of SSE
# instructions, symbols absent from Windows 95.
#
#   tools/win95/witnesses/build-witnesses.sh
#
# Candidates:
#   mingw-win32   GCC 13, i686-w64-mingw32, win32 threading model
#   mingw-posix   GCC 13, i686-w64-mingw32, winpthreads model
#   watcom        Open Watcom 2.0, nt target (386)
#
# Open Watcom has no <thread>: T3a does not exist for it, and that is the
# result, not a defect of this script.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
OUT="${DKR_WITNESS_OUT:-$ROOT/build/win95-witnesses}"
export PATH="$PREFIX/opt/mingw/usr/bin:$PREFIX/bin:$PATH"

# Instructions that -mno-sse must have eliminated. The check bears on the whole
# binary, startup code and standard library included: that is where SSE slips in,
# not in the code one writes.
SSE_RX='\b(movss|movsd|addss|addsd|mulss|mulsd|divss|divsd|cvtsi2s[sd]|cvtts?[sd]2si|comiss|ucomiss|comisd|ucomisd|movaps|movapd|movdqa|movdqu|xorps|xorpd|pxor|punpck|paddd|psubd|pshufd)\b'

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m   %s\033[0m\n' "$*"; }

mkdir -p "$OUT"

build_mingw() {  # $1 = model, $2 = witness, $3 = output
  local model="$1" w="$2" o="$3"
  local cc="i686-w64-mingw32-gcc-$model" cxx="i686-w64-mingw32-g++-$model"
  local common="-O2 -march=pentium2 -mtune=pentium3 -mfpmath=387 -mno-sse -mno-sse2"
  case "$w" in
    t1) $cc $common -nostdlib -nostartfiles -nodefaultlibs \
           -Wl,--subsystem,windows -Wl,-e,_start \
           -o "$o" "$HERE/t1.c" -lkernel32 -luser32 2>&1 ;;
    t2) $cc $common -static -static-libgcc \
           -o "$o" "$HERE/t2.c" -luser32 2>&1 ;;
    *)  $cxx -std=c++20 $common -static -static-libgcc -static-libstdc++ \
           -o "$o" "$HERE/$w.cpp" -luser32 2>&1 ;;
  esac
}

build_watcom() { # $1 = witness, $2 = output
  local w="$1" o="$2"
  export WATCOM="${DKR_WATCOM:-$PREFIX/opt/watcom}"
  export PATH="$WATCOM/binl64:$WATCOM/binl:$PATH"
  export INCLUDE="$WATCOM/h;$WATCOM/h/nt"
  local src="$HERE/$w.c"; [[ -f "$src" ]] || src="$HERE/$w.cpp"
  # -bt=nt targets Win32; -l=nt picks the link format and brings in the Win32
  # import libraries (without it, wlink does not resolve __imp__*);
  # -xs exceptions, -xr RTTI; -6s: Pentium Pro, x87-stack arithmetic.
  local flags="-bt=nt -l=nt -6s -otexan -q"
  [[ "$src" == *.cpp ]] && flags="$flags -xs -xr"
  ( cd "$(dirname "$o")" && wcl386 $flags -fe="$o" "$src" 2>&1 )
}

report() { # $1 = label, $2 = binary
  local label="$1" bin="$2"
  if [[ ! -f "$bin" ]]; then
    printf '  %-22s %-10s %s\n' "$label" "-" "NOT BUILT"; return
  fi
  local sz dlls nsym missing sse
  sz=$(stat -c%s "$bin")
  dlls=$(python3 "$ROOT/tools/win95/pe_symbols.py" --imports "$bin" | cut -f1 | sort -u | tr '\n' ' ')
  nsym=$(python3 "$ROOT/tools/win95/pe_symbols.py" --imports "$bin" | wc -l)
  # `check-win95-imports.sh` exits 1 when symbols are missing, and that is
  # precisely the case we want to report: neutralise the return code, without
  # which `set -e` stops the bench on the first interesting witness.
  #
  # The `MISSING` label is the one `check_imports.py` prints; the two must be
  # renamed together.
  missing=$( { "$ROOT/tools/win95/check-win95-imports.sh" "$bin" 2>/dev/null || true; } \
            | sed -E 's/\x1b\[[0-9;]*m//g' | awk '/^ *MISSING/ {print $2}' | tr '\n' ' ')
  if objdump -d -M intel "$bin" 2>/dev/null | grep -qE "$SSE_RX"; then sse="YES"; else sse="no"; fi
  printf '  %-22s %9d B  %3d sym  SSE:%-4s %s\n' "$label" "$sz" "$nsym" "$sse" "$dlls"
  [[ -n "$missing" ]] && warn "absent from Win95: $missing"
  return 0
}

for model in win32 posix; do
  say "mingw-$model candidate"
  for w in t1 t2 t3a t3b; do
    o="$OUT/${w}_$model.exe"
    rm -f "$o"
    if err=$(build_mingw "$model" "$w" "$o" 2>&1); then :; else
      printf '  %-22s %s\n' "$w" "BUILD FAILED"
      sed -E 's/^/      /' <<< "$(head -3 <<< "$err")"
      continue
    fi
    report "$w" "$o"
  done
done

say "watcom candidate"
WATCOM_DIR="${DKR_WATCOM:-$PREFIX/opt/watcom}"
if [[ ! -x "$WATCOM_DIR/binl64/wcl386" ]]; then
  warn "Open Watcom absent from $WATCOM_DIR - candidate skipped"
else
  for w in t1 t2 t3a t3b; do
    o="$OUT/${w}_watcom.exe"
    rm -f "$o"
    if [[ "$w" == "t3a" ]]; then
      printf '  %-22s %-10s %s\n' "$w" "-" "NOT APPLICABLE: Open Watcom has no <thread>"
      continue
    fi
    if err=$(build_watcom "$w" "$o" 2>&1); then :; else
      printf '  %-22s %s\n' "$w" "BUILD FAILED"
      grep -E 'Error|error' <<< "$err" | head -3 | sed -E 's/^/      /'
      continue
    fi
    report "$w" "$o"
  done
fi

echo
echo "binaries in $OUT"
