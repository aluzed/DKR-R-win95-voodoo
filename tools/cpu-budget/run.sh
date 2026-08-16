#!/usr/bin/env bash
# E00-S03 - Measures the cost of the recompiled code in 32-bit without SSE.
#
#   tools/cpu-budget/run.sh <path/to/dkr.us.v77.elf> [iterations]
#
# Builds the generated code twice, in 64-bit (the reference) and in 32-bit
# without SSE (the Windows 95 target), runs the same subset of leaf functions in
# both, and reports the factor per function.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HERE="$ROOT/tools/cpu-budget"
FUNCS="$ROOT/runtime-recomp/RecompiledFuncs"
RECOMP_INC="$ROOT/extern/n64-modern-runtime/N64Recomp/include"
WORK="${DKR_CPU_BUDGET_WORK:-$ROOT/build/cpu-budget}"

ELF="${1:-}"
ITER="${2:-3000000}"
[[ -n "$ELF" && -f "$ELF" ]] || { echo "usage: $0 <dkr.us.v77.elf> [iterations]" >&2; exit 2; }
[[ -d "$FUNCS" ]] || { echo "RecompiledFuncs missing: generate the sources first." >&2; exit 2; }

CFLAGS_COMMON="-O2 -fno-strict-aliasing -w -I$RECOMP_INC -I$FUNCS -I$WORK"
CFLAGS_32="-m32 -march=pentium2 -mtune=pentium3 -mfpmath=387 -mno-sse"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

mkdir -p "$WORK/obj64" "$WORK/obj32"

say "Equivalence proof for the portable 128-bit multiplication"
gcc -O2 -o "$WORK/dmult_test" "$HERE/dmult_test.c"
"$WORK/dmult_test"

say "Selecting the leaf functions"
python3 "$HERE/select_leaf_funcs.py" --funcs-dir "$FUNCS" --output "$WORK/bench_decls.h"

compile_set() { # $1=object directory  $2...=extra options
  local out="$1"; shift
  for f in "$FUNCS"/funcs_*.c; do
    gcc -c $CFLAGS_COMMON "$@" "$f" -o "$out/$(basename "$f" .c).o" &
  done
  wait
}

say "64-bit compilation (the reference)"
/usr/bin/time -f "  time: %e s, peak RSS: %M kB" bash -c "$(declare -f compile_set); \
  CFLAGS_COMMON='$CFLAGS_COMMON'; FUNCS='$FUNCS'; compile_set '$WORK/obj64'"

say "32-bit compilation without SSE (the target)"
/usr/bin/time -f "  time: %e s, peak RSS: %M kB" bash -c "$(declare -f compile_set); \
  CFLAGS_COMMON='$CFLAGS_COMMON $CFLAGS_32'; FUNCS='$FUNCS'; compile_set '$WORK/obj32'"

say "Code size"
printf '  .text 64-bit : %s\n' "$(size -t "$WORK"/obj64/*.o | tail -1 | awk '{print $1}')"
printf '  .text 32-bit : %s\n' "$(size -t "$WORK"/obj32/*.o | tail -1 | awk '{print $1}')"

say "Check: no SSE instruction in 32-bit"
if objdump -d "$WORK"/obj32/*.o | grep -qE '\b(movss|movsd|cvtsi2s[sd]|cvtts[sd]2si|pxor|movaps|xorps)\b'; then
  echo "  FAILED: SSE instructions remain" >&2; exit 1
fi
echo "  none"

say "Stubs for the symbols left to librecomp"
nm -u "$WORK"/obj64/*.o | awk '$1=="U"{print $2}' | sort -u > "$WORK/u.txt"
nm --defined-only "$WORK"/obj64/*.o | awk 'NF==3{print $3}' | sort -u > "$WORK/d.txt"
comm -23 "$WORK/u.txt" "$WORK/d.txt" \
  | comm -23 - <(nm -D --defined-only /lib/x86_64-linux-gnu/libc.so.6 /lib/x86_64-linux-gnu/libm.so.6 \
                 2>/dev/null | awk '{print $3}' | sed 's/@.*//' | sort -u) > "$WORK/ext.txt"
python3 - "$WORK/ext.txt" "$WORK/stubs.c" <<'PY'
import pathlib, re, sys
syms = [s for s in pathlib.Path(sys.argv[1]).read_text().split()
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", s)]
out = ['#include <stdio.h>', '#include <stdlib.h>', '#include <stdint.h>', '',
       'static void stub_reached(const char *n) {',
       '    fprintf(stderr, "\\nINVALID BENCH: call to %s\\n", n); abort(); }', '']
for s in syms:
    if s == "cop0_status_read":
        out.append('uint64_t cop0_status_read(void *c) { (void)c; return 0; }')
    elif s == "cop0_status_write":
        out.append('void cop0_status_write(void *c, uint64_t v) { (void)c; (void)v; }')
    else:
        out.append(f'void {s}(void) {{ stub_reached("{s}"); }}')
pathlib.Path(sys.argv[2]).write_text("\n".join(out) + "\n")
print(f"  {len(syms)} stubs")
PY

say "Linking"
gcc $CFLAGS_COMMON "$HERE/bench.c" "$WORK/stubs.c" "$WORK"/obj64/*.o -o "$WORK/bench64" -lm
gcc $CFLAGS_COMMON $CFLAGS_32 "$HERE/bench.c" "$WORK/stubs.c" "$WORK"/obj32/*.o -o "$WORK/bench32" -lm

say "Measurement - three runs of $ITER calls per function"
for r in 1 2 3; do
  "$WORK/bench64" "$ELF" "$ITER" 2>/dev/null > "$WORK/r64_$r.txt"
  "$WORK/bench32" "$ELF" "$ITER" 2>/dev/null > "$WORK/r32_$r.txt"
done

python3 - "$WORK" <<'PY'
import pathlib, statistics, sys
W = pathlib.Path(sys.argv[1])
def load(tag):
    runs = []
    for r in (1, 2, 3):
        d = {}
        for line in W.joinpath(f"r{tag}_{r}.txt").read_text().splitlines():
            p = line.split()
            if len(p) == 3 and p[1].isdigit():
                d[p[0]] = float(p[2])
        runs.append(d)
    return {k: min(run[k] for run in runs) for k in runs[0]}
a, b = load(64), load(32)
common = sorted(set(a) & set(b), key=lambda k: -(b[k] / a[k]))
print(f"\n{'function':<36}{'64b ns':>9}{'32b ns':>9}{'factor':>10}")
print("-" * 64)
for k in common:
    print(f"{k:<36}{a[k]:>9.2f}{b[k]:>9.2f}{b[k]/a[k]:>9.2f}x")
ta, tb = sum(a[k] for k in common), sum(b[k] for k in common)
ratios = sorted(b[k] / a[k] for k in common)
print("-" * 64)
print(f"{'TOTAL':<36}{ta:>9.2f}{tb:>9.2f}{tb/ta:>9.2f}x")
print(f"\nfunctions compared : {len(common)}")
print(f"median factor      : {statistics.median(ratios):.2f}x")
print(f"min / max factor   : {ratios[0]:.2f}x / {ratios[-1]:.2f}x")
PY
