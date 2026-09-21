#!/usr/bin/env bash
# Build DKR's recompiled sources at both guest-register widths and compare them.
#
# Reports, for each width: the emitted .text, the x86 instruction count, how many
# of those instructions reference memory, and the mix of the instructions that
# differ most.  Also runs the two tests that say the narrowing is sound: one on
# the header's macros, one on the three functions that need a wide path.
#
# See docs/research/cpu-budget.md, "Narrowing the guest register, done properly".

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${root}/build/cpu-budget"
include="${root}/extern/n64-modern-runtime/N64Recomp/include"
tools="${root}/tools/cpu-budget"
funcs="${root}/runtime-recomp/RecompiledFuncs"
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

# The narrowing lives behind a define in recomp.h itself, applied by
# patches/n64recomp/0004-narrow-the-guest-register-on-request.patch.
narrow_define="-DDKR_NARROW_GUEST_REGISTER"

mkdir -p "${build}"

# The three generated functions the native paths replace, lifted out of
# RecompiledFuncs so the fidelity test compares against what actually ships
# rather than against a copy that can drift.
echo "== extracting the three generated functions =="
python3 - "${funcs}" "${build}/generated_three.c" <<'PYTHON'
import pathlib
import sys

source = pathlib.Path(sys.argv[1])
destination = pathlib.Path(sys.argv[2])
wanted = {
    "atan2s": "funcs_9.c",
    "dmacopy_doubleword": "funcs_14.c",
    "rand_range": "funcs_26.c",
}
chunks = []
for name, filename in wanted.items():
    text = (source / filename).read_text(errors="replace")
    start = text.index(
        "RECOMP_FUNC void %s(uint8_t* rdram, recomp_context* ctx) {" % name)
    end = text.index("\n;}", start) + len("\n;}")
    body = text[start:end]
    # Renamed so both implementations can be linked and compared, and the hook
    # removed so the generated body is the thing under test.
    body = body.replace("void %s(" % name, "void generated_%s(" % name, 1)
    body = "\n".join(line for line in body.split("\n")
                     if "dkr_wide_" not in line)
    chunks.append(body)
destination.write_text('#include "recomp.h"\n\n' + "\n\n".join(chunks) + "\n")
print("  %d functions, %d bytes" % (len(chunks), destination.stat().st_size))
PYTHON

cat > "${build}/test_support.c" <<'SUPPORT'
/* The two entry points the extracted functions reach that the runtime would
   otherwise supply. */
#include "recomp.h"
#include <stdio.h>

void do_break(uint32_t vram) {
    printf("  do_break %08x\n", (unsigned)vram);
}

int dkr_netplay_presentation_random_range(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
    return 0;  /* Nothing has overridden the range. */
}
SUPPORT

run_test() {
    local name="$1" sources="$2"
    echo
    echo "== ${name} =="
    local width define transcripts=()
    for width in wide narrow; do
        define=""
        if [[ "${width}" == "narrow" ]]; then
            define="${narrow_define}"
        fi
        # shellcheck disable=SC2086
        gcc -m32 -O2 -Wall ${define} -I"${include}" -I"${tools}" \
            -o "${build}/${name}_${width}" ${sources} 2> "${build}/${name}_${width}.log"
        "${build}/${name}_${width}" > "${build}/${name}_${width}.txt"
        transcripts+=("$(grep '^transcript' "${build}/${name}_${width}.txt")")
    done
    if [[ "${transcripts[0]}" == "${transcripts[1]}" ]]; then
        echo "the two widths agree: ${transcripts[0]}"
    else
        echo "THE TWO WIDTHS DISAGREE"
        printf '  wide   %s\n  narrow %s\n' "${transcripts[0]}" "${transcripts[1]}"
        diff -u "${build}/${name}_wide.txt" "${build}/${name}_narrow.txt" || true
    fi
    grep -E '^(  atan2s|  rand_range|  dmacopy|generated against)' \
        "${build}/${name}_wide.txt" || true
}

run_test macros "${tools}/narrow_gpr_test.c"
run_test fidelity "${tools}/wide_fidelity_test.c \
    ${root}/runtime-recomp/src/game/runtime_wide_registers.c \
    ${build}/generated_three.c ${build}/test_support.c"

echo
echo "== building ${funcs##*/} at both widths =="
for width in wide narrow; do
    define=""
    if [[ "${width}" == "narrow" ]]; then
        define="${narrow_define}"
    fi
    objects="${build}/objects-${width}"
    rm -rf "${objects}"
    mkdir -p "${objects}"
    # -Wshift-count-overflow still fires on rand_range's generated body, which
    # the hook now makes unreachable. It is dead code that still compiles, so
    # the warning is real about the code and no longer about the behaviour.
    find "${funcs}" -name '*.c' -print0 \
        | xargs -0 -P "${jobs}" -I{} sh -c '
            name="$(basename "$1" .c)"
            gcc -m32 -O2 -c $5 -I"$2" -I"$3" -Wshift-count-overflow \
                -o "$4/${name}.o" "$1" 2> "$4/${name}.log"
        ' _ {} "${include}" "${funcs}" "${objects}" "${define}"

    objdump -d "${objects}"/*.o \
        | grep -E '^[[:space:]]+[0-9a-f]+:' > "${build}/disassembly-${width}.txt"
    awk -F'\t' 'NF>=3 { split($3, parts, " "); print parts[1] }' \
        "${build}/disassembly-${width}.txt" \
        | sort | uniq -c | sort -rn > "${build}/opcodes-${width}.txt"

    text="$(size -A "${objects}"/*.o | awk '$1 == ".text" { total += $2 } END { print total }')"
    instructions="$(wc -l < "${build}/disassembly-${width}.txt")"
    memory="$(grep -cE '\(%e[a-z]{2}' "${build}/disassembly-${width}.txt" || true)"
    warnings="$(cat "${objects}"/*.log | grep -c 'shift count' || true)"

    printf '%-7s .text %9d   instructions %8d   memory-referencing %8d   shift warnings %d\n' \
        "${width}" "${text}" "${instructions}" "${memory}" "${warnings}"
done

echo
echo "== the instructions that differ most =="
join -j 2 -o 0,1.1,2.1 \
    <(sort -k2 "${build}/opcodes-wide.txt") \
    <(sort -k2 "${build}/opcodes-narrow.txt") \
    | awk '{
        removed = $2 - $3
        if (removed > 2000) {
            printf "  %-6s %8d -> %8d  %+8d  %+6.1f%%\n", $1, $2, $3, -removed, -100.0 * removed / $2
        }
    }' | sort -k4 -n
