#!/usr/bin/env bash
# E01-S01 — verifie qu'un binaire n'utilise aucune instruction posterieure au
# Pentium II.
#
#   tools/win95/check-instruction-set.sh build/.../DKR-R.EXE
#   tools/win95/check-instruction-set.sh --self-test     (verifie le verificateur)
#
# Pourquoi ce controle est indispensable : un compilateur moderne en 32 bits
# emet du SSE2 par defaut pour l'arithmetique flottante. L'echec ne se voit pas
# a la compilation — il se voit au lancement, par une exception d'instruction
# invalide, eventuellement des mois plus tard dans une fonction rarement
# atteinte.
#
# Le controle porte sur le **binaire lie**, pas sur les objets du projet : le
# SSE viendrait du code de demarrage du CRT ou de la bibliotheque standard, que
# `-mno-sse` sur nos sources ne couvre pas.
#
# Ce que le Pentium II sait faire, et qui est donc autorise :
#   386/486 de base, x87, CMOV (Pentium Pro), MMX, FXSAVE, RDTSC, CPUID,
#   CMPXCHG8B, et le prefixe LOCK.
#
# Ce qui est refuse : SSE et au-dela, 3DNow!, et les instructions de barriere
# memoire introduites avec le Pentium III.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
export PATH="$PREFIX/opt/mingw/usr/bin:$PREFIX/bin:$PATH"
OBJDUMP="${DKR_OBJDUMP:-}"
if [[ -z "$OBJDUMP" ]]; then
  for c in i686-w64-mingw32-objdump objdump; do
    command -v "$c" >/dev/null && { OBJDUMP="$c"; break; }
  done
fi
[[ -n "$OBJDUMP" ]] || { echo "erreur: objdump introuvable" >&2; exit 2; }

# Presque toutes les instructions SSE et au-dela nomment un registre XMM, YMM ou
# ZMM : c'est le signal le plus sur, et il ne depend pas d'une liste de
# mnemoniques a tenir a jour.
RX_REG='%?[xyz]mm[0-9]'

# Celles qui n'en nomment pas doivent etre listees. Barrieres et prefetch sont
# arrivees avec le Pentium III ; la famille pf* est 3DNow! (AMD K6-2).
RX_MNEMO='\b(sfence|lfence|mfence|clflush|movnti|prefetchnta|prefetcht[012]|prefetchw|femms|pfadd|pfsub|pfmul|pfrcp|pfrsqrt|pfmax|pfmin|pfcmp[a-z]*|pi2fd|pf2id|pswapd|cmpxchg16b|xgetbv|vzeroupper)\b'

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
bad()  { printf '\033[1;31m   %s\033[0m\n' "$*"; }
good() { printf '\033[1;32m   %s\033[0m\n' "$*"; }

scan() { # $1 = binaire ; renvoie 1 si une instruction interdite est trouvee
  local bin="$1" dis hits
  dis="$("$OBJDUMP" -d "$bin" 2>/dev/null || true)"
  if [[ -z "$dis" ]]; then
    bad "$bin : desassemblage impossible"; return 1
  fi
  # Ne garder que les lignes d'instruction : « adresse: octets  mnemonique »,
  # **et seulement celles qui sont vraiment du code**.
  #
  # Le lieur place les tables d'exceptions — `.gcc_except_table`, `.eh_frame` —
  # a l'interieur de `.text`, et `objdump -d` les desassemble comme le reste.
  # Des octets de donnees s'y decodent alors en instructions : un binaire qui
  # emploie `std::filesystem::path` produisait ainsi « movaps %xmm0,(%eax) » et
  # « movnti », deux instructions posterieures au Pentium II, dans de la donnee
  # que le processeur n'execute jamais.
  #
  # Le faux positif n'est pas benin : il fait echouer un build correct, et la
  # reaction naturelle devant un garde-fou qui crie a tort est de le desactiver.
  # On suit donc le symbole courant et on ignore les regions de donnees.
  hits="$(awk -v rx_reg="$RX_REG" -v rx_mnemo="$RX_MNEMO" '
    /^[0-9a-fA-F]+ <.*>:/ {
      sym = $2
      # Les regions de donnees que le lieur loge dans .text.
      skip = (sym ~ /gcc_except_table|eh_frame|\.rdata|\.data|jcr|CRT\$/) ? 1 : 0
      next
    }
    skip { next }
    /^[[:space:]]*[0-9a-fA-F]+:/ {
      if ($0 ~ rx_reg || $0 ~ rx_mnemo) print
    }
  ' <<< "$dis" || true)"
  if [[ -n "$hits" ]]; then
    local n; n=$(wc -l <<< "$hits")
    bad "$(basename "$bin") : $n instruction(s) hors Pentium II"
    head -8 <<< "$hits" | sed -E 's/^[[:space:]]*/      /'
    [[ $n -gt 8 ]] && printf '      ... et %d autres\n' "$((n - 8))"
    return 1
  fi
  good "$(basename "$bin") : aucune instruction hors Pentium II"
  return 0
}

self_test() {
  # Critere d'acceptation : le verificateur doit echouer sur du SSE injecte
  # volontairement. Sans cette epreuve, un verificateur casse passerait pour un
  # verificateur satisfait.
  local tmp cc
  tmp="$(mktemp -d)"; trap 'rm -rf -- "$tmp"' RETURN
  cc="$(command -v i686-w64-mingw32-gcc-posix || command -v i686-w64-mingw32-gcc || command -v gcc)"

  cat > "$tmp/clean.c" <<'EOF'
double f(double a, double b) { return a * b + a / b; }
int main(void) { return (int)f(3.0, 7.0); }
EOF
  cat > "$tmp/dirty.c" <<'EOF'
#include <xmmintrin.h>
float g(float *p) { __m128 v = _mm_loadu_ps(p); v = _mm_add_ps(v, v); return _mm_cvtss_f32(v); }
int main(void) { float p[4] = {1,2,3,4}; return (int)g(p); }
EOF
  say "temoin propre : x87 seulement"
  "$cc" -O2 -march=pentium2 -mfpmath=387 -mno-sse -c -o "$tmp/clean.o" "$tmp/clean.c" 2>/dev/null
  scan "$tmp/clean.o" || { bad "le temoin propre est refuse — le verificateur est trop strict"; return 1; }

  say "temoin sale : SSE injecte volontairement"
  "$cc" -O2 -msse -c -o "$tmp/dirty.o" "$tmp/dirty.c" 2>/dev/null
  if scan "$tmp/dirty.o" >/dev/null 2>&1; then
    bad "le temoin sale est accepte — le verificateur ne detecte rien"
    return 1
  fi
  good "temoin sale correctement refuse"
  say "le verificateur fonctionne"
  return 0
}

if [[ "${1:-}" == "--self-test" ]]; then
  self_test; exit $?
fi
[[ $# -ge 1 ]] || { sed -n '2,12p' "$0"; exit 2; }

status=0
for bin in "$@"; do
  [[ -f "$bin" ]] || { bad "introuvable : $bin"; status=1; continue; }
  scan "$bin" || status=1
done
exit $status
