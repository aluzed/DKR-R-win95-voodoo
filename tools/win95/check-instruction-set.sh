#!/usr/bin/env bash
# E01-S01 - checks that a binary uses no instruction later than the Pentium II.
#
#   tools/win95/check-instruction-set.sh build/.../DKR-R.EXE
#   tools/win95/check-instruction-set.sh --self-test     (checks the checker)
#
# Why this check is indispensable: a modern 32-bit compiler emits SSE2 by
# default for floating-point arithmetic. The failure does not show at compile
# time - it shows at launch, as an invalid-instruction exception, possibly months
# later inside a rarely reached function.
#
# The check bears on the **linked binary**, not on the project's objects: the SSE
# would come from the CRT's startup code or from the standard library, which
# `-mno-sse` on our sources does not cover.
#
# What the Pentium II can do, and what is therefore allowed:
#   base 386/486, x87, CMOV (Pentium Pro), MMX, FXSAVE, RDTSC, CPUID,
#   CMPXCHG8B, and the LOCK prefix.
#
# What is refused: SSE and beyond, 3DNow!, and the memory-barrier instructions
# introduced with the Pentium III.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
export PATH="$PREFIX/opt/mingw/usr/bin:$PREFIX/bin:$PATH"
OBJDUMP="${DKR_OBJDUMP:-}"
if [[ -z "$OBJDUMP" ]]; then
  for c in i686-w64-mingw32-objdump objdump; do
    command -v "$c" >/dev/null && { OBJDUMP="$c"; break; }
  done
fi
[[ -n "$OBJDUMP" ]] || { echo "error: objdump not found" >&2; exit 2; }

# Almost every SSE-and-beyond instruction names an XMM, YMM or ZMM register:
# that is the surest signal, and it does not depend on a list of mnemonics that
# has to be kept up to date.
RX_REG='%?[xyz]mm[0-9]'

# The ones that name none have to be listed. Barriers and prefetch arrived with
# the Pentium III; the pf* family is 3DNow! (AMD K6-2).
RX_MNEMO='\b(sfence|lfence|mfence|clflush|movnti|prefetchnta|prefetcht[012]|prefetchw|femms|pfadd|pfsub|pfmul|pfrcp|pfrsqrt|pfmax|pfmin|pfcmp[a-z]*|pi2fd|pf2id|pswapd|cmpxchg16b|xgetbv|vzeroupper)\b'

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
bad()  { printf '\033[1;31m   %s\033[0m\n' "$*"; }
good() { printf '\033[1;32m   %s\033[0m\n' "$*"; }

scan() { # $1 = binary; returns 1 if a forbidden instruction is found
  local bin="$1" dis hits
  dis="$("$OBJDUMP" -d "$bin" 2>/dev/null || true)"
  if [[ -z "$dis" ]]; then
    bad "$bin: cannot disassemble"; return 1
  fi
  # Keep only the instruction lines - "address: bytes  mnemonic" - **and only
  # those that are really code**.
  #
  # The linker places the exception tables - `.gcc_except_table`, `.eh_frame` -
  # inside `.text`, and `objdump -d` disassembles them like the rest. Data bytes
  # then decode as instructions: a binary using `std::filesystem::path` produced
  # "movaps %xmm0,(%eax)" and "movnti" that way, two post-Pentium II
  # instructions, inside data the processor never executes.
  #
  # The false positive is not harmless: it fails a correct build, and the natural
  # reaction to a guard rail that cries wolf is to disable it. So we track the
  # current symbol and skip the data regions.
  hits="$(awk -v rx_reg="$RX_REG" -v rx_mnemo="$RX_MNEMO" '
    /^[0-9a-fA-F]+ <.*>:/ {
      sym = $2
      # The data regions the linker lodges inside .text.
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
    bad "$(basename "$bin"): $n instruction(s) outside the Pentium II set"
    head -8 <<< "$hits" | sed -E 's/^[[:space:]]*/      /'
    [[ $n -gt 8 ]] && printf '      ... and %d more\n' "$((n - 8))"
    return 1
  fi
  good "$(basename "$bin"): no instruction outside the Pentium II set"
  return 0
}

self_test() {
  # Acceptance criterion: the checker must fail on deliberately injected SSE.
  # Without this test, a broken checker would pass for a satisfied one.
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
  say "clean witness: x87 only"
  "$cc" -O2 -march=pentium2 -mfpmath=387 -mno-sse -c -o "$tmp/clean.o" "$tmp/clean.c" 2>/dev/null
  scan "$tmp/clean.o" || { bad "the clean witness is refused - the checker is too strict"; return 1; }

  say "dirty witness: deliberately injected SSE"
  "$cc" -O2 -msse -c -o "$tmp/dirty.o" "$tmp/dirty.c" 2>/dev/null
  if scan "$tmp/dirty.o" >/dev/null 2>&1; then
    bad "the dirty witness is accepted - the checker detects nothing"
    return 1
  fi
  good "dirty witness correctly refused"
  say "the checker works"
  return 0
}

if [[ "${1:-}" == "--self-test" ]]; then
  self_test; exit $?
fi
[[ $# -ge 1 ]] || { sed -n '2,12p' "$0"; exit 2; }

status=0
for bin in "$@"; do
  [[ -f "$bin" ]] || { bad "not found: $bin"; status=1; continue; }
  scan "$bin" || status=1
done
exit $status
