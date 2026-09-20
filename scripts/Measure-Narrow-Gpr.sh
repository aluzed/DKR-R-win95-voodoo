#!/usr/bin/env bash
# Build DKR's recompiled sources at both guest-register widths and compare them.
#
# Reports, for each width: the emitted .text, the x86 instruction count, how many
# of those instructions reference memory, and the mix of the instructions that
# differ most.  Also runs the equivalence test that says whether the narrowed
# header computes the same values as the upstream one.
#
# See docs/research/cpu-budget.md, "Narrowing the guest register, done properly".

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${root}/build/cpu-budget"
upstream_include="${root}/extern/n64-modern-runtime/N64Recomp/include"
narrow_include="${build}/narrow-inc"
funcs="${root}/runtime-recomp/RecompiledFuncs"
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

mkdir -p "${narrow_include}" "${build}"

echo "== deriving the narrowed header =="
python3 "${root}/tools/cpu-budget/make_narrow_header.py" \
    "${upstream_include}/recomp.h" "${narrow_include}/recomp.h"

echo
echo "== equivalence test =="
for width in wide narrow; do
    case "${width}" in
        wide)   include="${upstream_include}" ;;
        narrow) include="${narrow_include}" ;;
    esac
    gcc -m32 -O2 -Wall -I"${include}" \
        -o "${build}/narrow_gpr_test_${width}" \
        "${root}/tools/cpu-budget/narrow_gpr_test.c"
    "${build}/narrow_gpr_test_${width}" > "${build}/narrow_gpr_test_${width}.txt"
done

wide_transcript="$(grep '^transcript' "${build}/narrow_gpr_test_wide.txt")"
narrow_transcript="$(grep '^transcript' "${build}/narrow_gpr_test_narrow.txt")"
if [[ "${wide_transcript}" == "${narrow_transcript}" ]]; then
    echo "the two widths agree: ${wide_transcript}"
else
    echo "THE TWO WIDTHS DISAGREE"
    echo "  wide   ${wide_transcript}"
    echo "  narrow ${narrow_transcript}"
    diff -u "${build}/narrow_gpr_test_wide.txt" \
            "${build}/narrow_gpr_test_narrow.txt" || true
fi

echo
echo "== building ${funcs##*/} at both widths =="
for width in wide narrow; do
    case "${width}" in
        wide)   include="${upstream_include}" ;;
        narrow) include="${narrow_include}" ;;
    esac
    objects="${build}/objects-${width}"
    rm -rf "${objects}"
    mkdir -p "${objects}"
    # -Wshift-count-overflow finds the dsll32/dsrl32 sites on its own; the other
    # 64-bit opcodes compile silently at either width, so the census in the
    # research note is what finds those.
    find "${funcs}" -name '*.c' -print0 \
        | xargs -0 -P "${jobs}" -I{} sh -c '
            name="$(basename "$1" .c)"
            gcc -m32 -O2 -c -I"$2" -I"$3" -Wshift-count-overflow \
                -o "$4/${name}.o" "$1" 2> "$4/${name}.log"
        ' _ {} "${include}" "${funcs}" "${objects}"

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
