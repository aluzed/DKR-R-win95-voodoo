#!/usr/bin/env bash
# E00-S02 — construit les temoins T1/T2/T3 avec chaque candidat de chaine de
# compilation, et rapporte pour chacun : taille, DLL et symboles importes,
# presence d'instructions SSE, symboles absents de Windows 95.
#
#   tools/win95/witnesses/build-witnesses.sh
#
# Candidats :
#   mingw-win32   GCC 13, i686-w64-mingw32, modele de threads win32
#   mingw-posix   GCC 13, i686-w64-mingw32, modele winpthreads
#   watcom        Open Watcom 2.0, cible nt (386)
#
# Open Watcom n'a pas <thread> : T3a n'existe pas pour lui, et c'est le
# resultat, pas un defaut du script.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
OUT="${DKR_WITNESS_OUT:-$ROOT/build/win95-witnesses}"
export PATH="$PREFIX/opt/mingw/usr/bin:$PREFIX/bin:$PATH"

# Instructions que -mno-sse doit avoir eliminees. La verification porte sur le
# binaire entier, code de demarrage et bibliotheque standard compris : c'est la
# que le SSE se glisse, pas dans le code qu'on ecrit.
SSE_RX='\b(movss|movsd|addss|addsd|mulss|mulsd|divss|divsd|cvtsi2s[sd]|cvtts?[sd]2si|comiss|ucomiss|comisd|ucomisd|movaps|movapd|movdqa|movdqu|xorps|xorpd|pxor|punpck|paddd|psubd|pshufd)\b'

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m   %s\033[0m\n' "$*"; }

mkdir -p "$OUT"

build_mingw() {  # $1 = modele, $2 = temoin, $3 = sortie
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

build_watcom() { # $1 = temoin, $2 = sortie
  local w="$1" o="$2"
  export WATCOM="${DKR_WATCOM:-$PREFIX/opt/watcom}"
  export PATH="$WATCOM/binl64:$WATCOM/binl:$PATH"
  export INCLUDE="$WATCOM/h;$WATCOM/h/nt"
  local src="$HERE/$w.c"; [[ -f "$src" ]] || src="$HERE/$w.cpp"
  # -bt=nt cible Win32 ; -l=nt choisit le format d'edition de liens et amene les
  # bibliotheques d'import Win32 (sans lui, wlink ne resout pas __imp__*) ;
  # -xs exceptions, -xr RTTI ; -6s : Pentium Pro, arithmetique sur pile x87.
  local flags="-bt=nt -l=nt -6s -otexan -q"
  [[ "$src" == *.cpp ]] && flags="$flags -xs -xr"
  ( cd "$(dirname "$o")" && wcl386 $flags -fe="$o" "$src" 2>&1 )
}

report() { # $1 = etiquette, $2 = binaire
  local label="$1" bin="$2"
  if [[ ! -f "$bin" ]]; then
    printf '  %-22s %-10s %s\n' "$label" "-" "NON CONSTRUIT"; return
  fi
  local sz dlls nsym missing sse
  sz=$(stat -c%s "$bin")
  dlls=$(python3 "$ROOT/tools/win95/pe_symbols.py" --imports "$bin" | cut -f1 | sort -u | tr '\n' ' ')
  nsym=$(python3 "$ROOT/tools/win95/pe_symbols.py" --imports "$bin" | wc -l)
  # `check-win95-imports.sh` sort en 1 quand il manque des symboles, et c'est
  # justement le cas qu'on veut rapporter : neutraliser le code de retour, sans
  # quoi `set -e` interrompt le banc sur le premier temoin interessant.
  missing=$( { "$ROOT/tools/win95/check-win95-imports.sh" "$bin" 2>/dev/null || true; } \
            | sed -E 's/\x1b\[[0-9;]*m//g' | awk '/^ *ABSENT/ {print $2}' | tr '\n' ' ')
  if objdump -d -M intel "$bin" 2>/dev/null | grep -qE "$SSE_RX"; then sse="OUI"; else sse="non"; fi
  printf '  %-22s %9d o  %3d sym  SSE:%-4s %s\n' "$label" "$sz" "$nsym" "$sse" "$dlls"
  [[ -n "$missing" ]] && warn "absents de Win95 : $missing"
  return 0
}

for model in win32 posix; do
  say "candidat mingw-$model"
  for w in t1 t2 t3a t3b; do
    o="$OUT/${w}_$model.exe"
    rm -f "$o"
    if err=$(build_mingw "$model" "$w" "$o" 2>&1); then :; else
      printf '  %-22s %s\n' "$w" "COMPILATION ECHOUEE"
      sed -E 's/^/      /' <<< "$(head -3 <<< "$err")"
      continue
    fi
    report "$w" "$o"
  done
done

say "candidat watcom"
WATCOM_DIR="${DKR_WATCOM:-$PREFIX/opt/watcom}"
if [[ ! -x "$WATCOM_DIR/binl64/wcl386" ]]; then
  warn "Open Watcom absent de $WATCOM_DIR — candidat ignore"
else
  for w in t1 t2 t3a t3b; do
    o="$OUT/${w}_watcom.exe"
    rm -f "$o"
    if [[ "$w" == "t3a" ]]; then
      printf '  %-22s %-10s %s\n' "$w" "-" "SANS OBJET : Open Watcom n'a pas <thread>"
      continue
    fi
    if err=$(build_watcom "$w" "$o" 2>&1); then :; else
      printf '  %-22s %s\n' "$w" "COMPILATION ECHOUEE"
      grep -E 'Error|error' <<< "$err" | head -3 | sed -E 's/^/      /'
      continue
    fi
    report "$w" "$o"
  done
fi

echo
echo "binaires dans $OUT"
