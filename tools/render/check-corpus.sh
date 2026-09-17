#!/usr/bin/env bash
# E09-S02 - replays every capture of a corpus through the oracle and reports what
# changed.
#
#   tools/render/check-corpus.sh <corpus-dir>
#   tools/render/check-corpus.sh <corpus-dir> --accept     adopt what came out
#
# **Why the corpus lives outside the repository.** A capture is the whole of
# RDRAM, eight mebibytes, and it has to be: the decoder reads at addresses the
# list itself computes, so there is no knowing in advance which bytes matter. A
# dozen scenes is a hundred megabytes, which does not belong in git. The captures
# and their reference images sit in a directory the user keeps; what is versioned
# is this script and the counts, which are text.
#
# ## What it checks, and in which order
#
#  1. **The decoder's counts** - commands, triangles, emitted, rejects, textures.
#     Checked first, because a count that moved is a decoder or determinism
#     change, and an image difference downstream of one says nothing about
#     rendering. This is the check that survives without a reference image.
#  2. **The image**, against `<name>.bmp` if it is there, with the metric of
#     `platform/render/imagecmp.c` - the same one the target's `REPLAY.EXE` and
#     `COMPARE.EXE` use.
#
# ## The threshold is per scene, and it is a file
#
# `<name>.threshold` holds `<max-gap> <ppm>` on one line. Absent, the defaults
# below apply. Per scene rather than global because scenes are not equally close:
# one that is all flat colour agrees to the bit, one full of gradients does not,
# and a single threshold either passes the second or fails the first for ever.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TOOLS="${DKR_RENDER_TOOLS_OUT:-$ROOT/build/render-tools}"

DEFAULT_MAX_GAP=16
DEFAULT_PPM=10000

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
pass() { printf '  \033[1;32mok  \033[0m %s\n' "$*"; }
fail() { printf '  \033[1;31mFAIL\033[0m %s\n' "$*"; }
note() { printf '  \033[1;33mnew \033[0m %s\n' "$*"; }

corpus="${1:-}"
accept=0
[[ "${2:-}" == "--accept" ]] && accept=1

if [[ -z "$corpus" || ! -d "$corpus" ]]; then
  printf 'usage: %s <corpus-dir> [--accept]\n' "$0" >&2
  exit 2
fi
if [[ ! -x "$TOOLS/replay" || ! -x "$TOOLS/compare" ]]; then
  printf 'error: build the host tools first: tools/render/build-host-tools.sh\n' >&2
  exit 2
fi

shopt -s nullglob nocaseglob
captures=("$corpus"/*.bin)
shopt -u nocaseglob
if [[ ${#captures[@]} -eq 0 ]]; then
  printf 'error: no capture (*.bin) in %s\n' "$corpus" >&2
  exit 2
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fails=0
news=0
checked=0

for cap in "${captures[@]}"; do
  name="$(basename "${cap%.*}")"
  say "$name"

  out="$work/$name.bmp"
  log="$work/$name.log"
  if ! "$TOOLS/replay" "$cap" "$out" >"$log" 2>&1; then
    fail "the replay refused it"
    sed 's/^/       /' "$log"
    fails=$((fails + 1))
    continue
  fi

  # --- 1. The counts ---------------------------------------------------------
  # Everything from `cmd=` to the end of the line, whatever fields the replay
  # prints. Naming them here once meant that adding `culled` and `clipped` to the
  # accounting made every scene fail with "printed no counts" — which the check
  # did catch, and which is not what it is for.
  counts="$(grep -o 'cmd=.*' "$log" | head -1)"
  ref_counts="$corpus/$name.counts"
  if [[ -z "$counts" ]]; then
    fail "the replay printed no counts"
    fails=$((fails + 1))
    continue
  fi
  if [[ -f "$ref_counts" ]]; then
    if [[ "$counts" == "$(cat "$ref_counts")" ]]; then
      pass "counts: $counts"
    else
      fail "counts changed"
      printf '       was  %s\n' "$(cat "$ref_counts")"
      printf '       now  %s\n' "$counts"
      fails=$((fails + 1))
      [[ $accept -eq 1 ]] && printf '%s\n' "$counts" >"$ref_counts"
    fi
  else
    note "counts: $counts"
    news=$((news + 1))
    [[ $accept -eq 1 ]] && printf '%s\n' "$counts" >"$ref_counts"
  fi

  # --- 1b. The decoder's own honesty -----------------------------------------
  #
  # The image check below compares the card against the oracle, and both are fed
  # by this same decoder: a configuration it resolves wrongly is rendered wrongly
  # and *identically* by both, and the comparison reports perfect agreement. The
  # hub's grey rectangles sat in the corpus that way, at 165 divergent pixels of
  # 307,200, with both backends wrong in the same place.
  #
  # These two numbers do not depend on either backend's output. They are the
  # decoder saying how many pixels it painted through a configuration it knows it
  # does not implement exactly - `approximate` - and through one it splits into
  # passes. A configuration quietly becoming approximate moves them, and nothing
  # watched them until now.
  #
  # It does not make this harness measure correctness. Nothing here can. It
  # watches the one honest signal that is not two things sharing a decoder.
  fill="$(grep -oE '(approximate|multipass) +[0-9]+' "$log" | tr '\n' ' ' | sed 's/ *$//')"
  ref_fill="$corpus/$name.fill"
  if [[ -n "$fill" ]]; then
    if [[ -f "$ref_fill" ]]; then
      if [[ "$fill" == "$(cat "$ref_fill")" ]]; then
        pass "fill: $fill"
      else
        fail "the decoder's approximate/multipass fill changed"
        printf '       was  %s\n' "$(cat "$ref_fill")"
        printf '       now  %s\n' "$fill"
        fails=$((fails + 1))
        [[ $accept -eq 1 ]] && printf '%s\n' "$fill" >"$ref_fill"
      fi
    else
      note "fill: $fill"
      news=$((news + 1))
      [[ $accept -eq 1 ]] && printf '%s\n' "$fill" >"$ref_fill"
    fi
  fi

  # --- 2. The image ----------------------------------------------------------
  ref_img="$corpus/$name.bmp"
  if [[ ! -f "$ref_img" ]]; then
    note "no reference image"
    [[ $accept -eq 1 ]] && cp "$out" "$ref_img"
    continue
  fi

  max_gap=$DEFAULT_MAX_GAP
  ppm=$DEFAULT_PPM
  if [[ -f "$corpus/$name.threshold" ]]; then
    read -r max_gap ppm < "$corpus/$name.threshold"
  fi

  checked=$((checked + 1))
  if "$TOOLS/compare" --max-gap "$max_gap" --differ-ppm "$ppm" \
        "$ref_img" "$out" "$work/$name.diff.bmp" >"$work/$name.cmp" 2>&1; then
    pass "$(grep -E 'frankly different' "$work/$name.cmp" | sed 's/^ *//')"
  else
    fail "$(grep -E 'VERDICT' "$work/$name.cmp" | sed 's/^ *//')"
    grep -E 'painted|frankly|edge|worst' "$work/$name.cmp" | sed 's/^ */       /'
    # The difference map is the only thing worth keeping from a failure, and it
    # is kept where the corpus is rather than in a temporary directory that the
    # trap above is about to remove.
    cp "$work/$name.diff.bmp" "$corpus/$name.diff.bmp" 2>/dev/null && \
      printf '       difference map: %s\n' "$corpus/$name.diff.bmp"
    fails=$((fails + 1))
    [[ $accept -eq 1 ]] && cp "$out" "$ref_img"
  fi
done

printf '\n'
say "$checked image(s) compared, $news without a reference, $fails failure(s)"
if [[ $accept -eq 1 ]]; then
  say "--accept: what came out is now the reference"
  exit 0
fi
exit $(( fails > 0 ? 1 : 0 ))
