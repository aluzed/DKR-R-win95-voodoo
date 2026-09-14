#!/usr/bin/env bash
# E09-S02 - builds the host side of the visual comparison harness.
#
#   tools/render/build-host-tools.sh
#
# Produces two programs in build/render-tools:
#
#   replay    renders a capture through E04-S08's software oracle
#   compare   measures the gap between two BMPs and draws where it is
#   texscore  scores a conversion rule over two directories of dumped textures
#
# **Why a script and not the CMake build.** `cmake/win95-target.cmake` builds for
# the Windows 95 target through a cross toolchain; these two run on the
# development machine, on the same sources, and adding a second toolchain to that
# file would make every target build carry a host compiler it does not need.
#
# The sources are listed rather than globbed. A glob would quietly pick up the
# next file added to `platform/render/`, and the first sign of it would be a link
# error in a build nobody changed.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${DKR_RENDER_TOOLS_OUT:-$ROOT/build/render-tools}"
CC="${CC:-gcc}"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

# The decoder and everything it pulls in. `imagecmp.c` is shared with the
# target's `COMPARE.EXE`, which is the point of it being a file at all.
CHAIN=(
  "$ROOT/platform/render/capture.c"
  "$ROOT/platform/render/clip.c"
  "$ROOT/platform/render/combiner.c"
  "$ROOT/platform/render/f3ddkr.c"
  "$ROOT/platform/render/imagecmp.c"
  "$ROOT/platform/render/rdp_state.c"
  "$ROOT/platform/render/software.c"
  "$ROOT/platform/render/texture.c"
  "$ROOT/platform/render/transform.c"
)

mkdir -p "$OUT"

# `-Werror`: these two decide whether an image is right, and a warning in a
# measuring instrument is the class of defect this repository keeps finding.
FLAGS=(-O2 -Wall -Wextra -Werror -I"$ROOT/platform")

say "replay"
"$CC" "${FLAGS[@]}" -o "$OUT/replay" "$HERE/replay.c" "${CHAIN[@]}" -lm

say "compare"
"$CC" "${FLAGS[@]}" -o "$OUT/compare" "$HERE/compare.c" \
      "$ROOT/platform/render/imagecmp.c" -lm

say "texscore"
"$CC" "${FLAGS[@]}" -o "$OUT/texscore" "$HERE/texscore.c" \
      "$ROOT/platform/render/imagecmp.c" -lm

say "built in $OUT"
