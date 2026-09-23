#!/usr/bin/env bash
# Builds the host-side audio tools (E03-S01, E03-S03) into build/audio-tools.
#
#   replay_aspmain        replays captured audio tasks through dkrAspMain, SIMD
#   replay_aspmain_sisd   the same through the scalar vector path the target uses
#   vu_difftest_simd/_sisd  one instruction at a time, to diff against each other
#
# The scalar path is selected by compiling against a copy of librecomp's headers
# in which rsp_vu.hpp does not detect SSE4.1: the choice is made by the header at
# compile time, and a Pentium II is the only machine that takes the other branch.
#
# Everything is built with -fno-strict-aliasing, as the target's RSP code now is:
# without it the scalar path is undefined behaviour (see cmake/win95-target.cmake).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="$root/build/audio-tools"
rt="$root/extern/n64-modern-runtime"
mkdir -p "$out"

sisd="$out/sisd-include"
rm -rf "$sisd"
cp -r "$rt/librecomp/include" "$sisd"
python3 - "$sisd/librecomp/rsp_vu.hpp" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()
for old in ("#if defined(__x86_64__) || defined(_M_X64)",
            "#elif defined(__aarch64__) || defined(_M_ARM64)"):
    assert old in text, old
    text = text.replace(old, "#if 0 /* scalar path forced */" if old.startswith("#if") else "#elif 0", 1)
open(path, "w").write(text)
PY

common=(-std=c++20 -O2 -fno-strict-aliasing -msse4.1 -w
        -I "$rt/ultramodern/include" -I "$rt/N64Recomp/include")
simd=(-I "$rt/librecomp/include")
scalar=(-I "$sisd")

g++ "${common[@]}" "${simd[@]}" -c "$root/runtime-recomp/RecompiledRSP/aspMain.cpp" -o "$out/aspMain.o"
g++ "${common[@]}" "${simd[@]}" "$root/tools/audio/replay_aspmain.cpp" "$out/aspMain.o" \
    -o "$out/replay_aspmain"
g++ "${common[@]}" "${scalar[@]}" -c "$root/runtime-recomp/RecompiledRSP/aspMain.cpp" \
    -o "$out/aspMain_sisd.o"
g++ "${common[@]}" "${scalar[@]}" "$root/tools/audio/replay_aspmain.cpp" "$out/aspMain_sisd.o" \
    -o "$out/replay_aspmain_sisd"
g++ "${common[@]}" "${simd[@]}" "$root/tools/audio/vu_difftest.cpp" -o "$out/vu_difftest_simd"
g++ "${common[@]}" "${scalar[@]}" "$root/tools/audio/vu_difftest.cpp" -o "$out/vu_difftest_sisd"


# The high-level mixer, command by command against the SIMD oracle.
gcc -std=c99 -O2 -fno-strict-aliasing -w -c "$root/platform/audio/aspmain_hle.c" -o "$out/aspmain_hle.o"
g++ "${common[@]}" "${simd[@]}" -I "$root/platform/audio" "$root/tools/audio/abi_difftest.cpp" \
    "$out/aspMain.o" "$out/aspmain_hle.o" -o "$out/abi_difftest"
echo "built abi_difftest"
