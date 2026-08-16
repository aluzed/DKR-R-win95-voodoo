#!/usr/bin/env bash
# Platform-layer tests that run on the host.
#
#   platform/win95/tests/run-tests.sh                  every suite
#   platform/win95/tests/run-tests.sh tick64           just one
#   platform/win95/tests/run-tests.sh threading
#   DKR_STRESS_SECONDS=600 platform/win95/tests/run-tests.sh threading
#
# Several suites, for different reasons:
#
#   tick64     (E01-S03) GetTickCount's wraparound happens after 49.7 days.
#              Waiting is not a protocol: the logic is a pure function, driven
#              here with chosen values.
#
#   clock      (E02-S03) the time base. Its delicate part - the conversion to
#              the VR4300 counter and the 32-bit wraparound - is made of pure
#              functions, hence entirely drivable here.
#
#   threading  (E02-S01) the same source as THREADS.EXE, which runs under
#              emulated Windows 95. Here it rests on threading.cpp's POSIX
#              vehicle, which makes the debugging cycle short. Passing here
#              proves nothing about the target - that is why the same binary is
#              also run on the machine.
#
# CTest registers them separately, so that `-R` can aim at one.
set -euo pipefail

suite="${1:-all}"
case "$suite" in
  all|tick64|threading|clock|fileio|saves|render|rdp|f3ddkr|transform|clip|pipeline|tmu|combiner|texture) ;;
  *) echo "usage: $0 [all|tick64|threading|clock|fileio|saves|render|rdp|f3ddkr|transform|clip|pipeline|tmu|combiner|texture]" >&2; exit 2 ;;
esac

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CC="${CC:-cc}"
CXX="${CXX:-c++}"

tmp="$(mktemp -d)"; trap 'rm -rf -- "$tmp"' EXIT

# --- E01-S03: the clock wraparound -------------------------------------------

if [[ "$suite" == "all" || "$suite" == "tick64" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "error: no host C compiler ($CC)" >&2; exit 2; }
  "$CC" -O2 -Wall -Wextra -o "$tmp/test_tick64" \
        "$HERE/test_tick64.c" "$HERE/../tick64.c"
  "$tmp/test_tick64"
fi

# --- E02-S01: threads and synchronisation ------------------------------------

if [[ "$suite" == "all" || "$suite" == "threading" ]]; then
  command -v "$CXX" >/dev/null \
    || { echo "error: no host C++ compiler ($CXX)" >&2; exit 2; }

  # `timeout` is not a convenience but the deadlock detector: a lost wake-up does
  # not produce a wrong result, it produces a wait that never ends. Verified by
  # injecting the loss of one wake-up in a thousand, which does make this timeout
  # fire. Without it, the suite would hang instead of failing. On macOS it comes
  # from coreutils, under the name `gtimeout`.
  TIMEOUT="$(command -v timeout || command -v gtimeout || true)"
  [[ -n "$TIMEOUT" ]] || {
    echo "error: 'timeout' is missing - it is what turns a deadlock into a" >&2
    echo "       failure. On macOS: brew install coreutils (gtimeout)." >&2
    exit 2
  }

  "$CXX" -O2 -Wall -Wextra -o "$tmp/test_threading" \
         "$HERE/test_threading.cpp" "$HERE/../threading.cpp" -lpthread

  stress_seconds="${DKR_STRESS_SECONDS:-0}"
  args=()
  limit=120
  if [[ "$stress_seconds" -gt 0 ]]; then
    args=(--stress "$stress_seconds")
    limit=$(( stress_seconds + 120 ))   # enough to finish the current round
  fi

  echo
  # `${args[@]+...}` and not `"${args[@]}"`: on an empty array the latter is an
  # unbound variable for bash 3.2, the one macOS still ships.
  "$TIMEOUT" "${limit}s" "$tmp/test_threading" ${args[@]+"${args[@]}"} || {
    rc=$?
    [[ $rc -eq 124 ]] && echo "FAIL: timed out - deadlock or lost wake-up" >&2
    exit $rc
  }
fi

# --- E02-S03: the time base --------------------------------------------------

if [[ "$suite" == "all" || "$suite" == "clock" ]]; then
  command -v "$CXX" >/dev/null \
    || { echo "error: no host C++ compiler ($CXX)" >&2; exit 2; }
  "$CXX" -O2 -Wall -Wextra -o "$tmp/test_clock" \
         "$HERE/test_clock.cpp" "$HERE/../clock.cpp" "$HERE/../tick64.c"
  echo
  "$tmp/test_clock"
fi

# --- E02-S05: durable writing ------------------------------------------------

if [[ "$suite" == "all" || "$suite" == "fileio" ]]; then
  command -v "$CXX" >/dev/null \
    || { echo "error: no host C++ compiler ($CXX)" >&2; exit 2; }
  "$CXX" -O2 -Wall -Wextra -o "$tmp/test_fileio" \
         "$HERE/test_fileio.cpp" "$HERE/../fileio.cpp"
  # The indirection point is tested **twice** on the host, and it is the second
  # arrangement that counts most.
  #
  #   without DKR_TARGET_WIN95: the std:: branch, the modern targets' one
  #   with    DKR_TARGET_WIN95: the Windows 95 branch, on the POSIX backend
  #
  # The second is not the real target - the POSIX backend is a vehicle - but it
  # runs the *logic* of the Windows 95 branch: the return values, the
  # conversions, the order of calls. That is where a contract divergence shows,
  # and without waiting for a twenty-minute round trip to the emulated machine.
  #
  # Both defects found while extending the layer would have been caught here: the
  # `create_directories` that returned true on a present directory, and the
  # `weakly_canonical` that did not resolve "..". The second actually was, on the
  # very first run of this arrangement.
  "$CXX" -O2 -Wall -Wextra -I"$HERE/.." -o "$tmp/test_fileio_seam" \
         "$HERE/test_fileio_seam.cpp" "$HERE/../fileio.cpp"
  "$CXX" -O2 -Wall -Wextra -DDKR_TARGET_WIN95 -I"$HERE/.." \
         -o "$tmp/test_fileio_seam95" \
         "$HERE/test_fileio_seam.cpp" "$HERE/../fileio.cpp"
  # The signatures used by the four files that only RT64 builds, and which
  # therefore cannot be compiled here. Nothing runs: compilation, in both
  # branches, is the check.
  "$CXX" -std=c++20 -Wall -Wextra -fsyntax-only -I"$HERE/.." \
         "$HERE/test_fileio_signatures.cpp"
  "$CXX" -std=c++20 -Wall -Wextra -fsyntax-only -DDKR_TARGET_WIN95 -I"$HERE/.." \
         "$HERE/test_fileio_signatures.cpp"
  echo "  ok    the game's signatures compile in both branches"

  echo
  # The test files are created in the current directory: we isolate it.
  ( cd "$tmp" && "$tmp/test_fileio" )
  echo "  -- indirection point, modern-targets branch --"
  ( cd "$tmp" && "$tmp/test_fileio_seam" )
  echo "  -- indirection point, Windows 95 branch on the POSIX backend --"
  ( cd "$tmp" && "$tmp/test_fileio_seam95" )
fi

# --- E02-S05: the save suites, on the host ------------------------------------
#
# They are compiled for the target and run on the machine - that is where the
# acceptance criteria close - but running them here as well shortens the cycle
# from several minutes to a second, and catches contract divergences before the
# round trip.
#
# `save_manager` is built **twice**, like the indirection point: the modern
# targets' branch, then the Windows 95 branch on the POSIX backends. It was the
# second arrangement that caught the create_directories returning true on a
# present directory.
if [[ "$suite" == "all" || "$suite" == "saves" ]]; then
  command -v "$CXX" >/dev/null \
    || { echo "error: no host C++ compiler ($CXX)" >&2; exit 2; }
  GAME="$HERE/../../../runtime-recomp/src/game"
  TESTS="$HERE/../../../runtime-recomp/tests"
  echo
  "$CXX" -O2 -Wall -Wextra -std=c++20 -I"$GAME" -I"$HERE/../.." -I"$HERE/.." \
         -o "$tmp/save_codec" "$TESTS/dkr_save_codec_tests.cpp" "$GAME/dkr_save_codec.cpp"
  "$tmp/save_codec"
  for mode in "" "-DDKR_TARGET_WIN95=1"; do
    label="modern-targets branch"
    [[ -n "$mode" ]] && label="Windows 95 branch on the POSIX backends"
    "$CXX" -O2 -Wall -Wextra -std=c++20 $mode -I"$GAME" -I"$HERE/../.." -I"$HERE/.." \
           -o "$tmp/save_manager" \
           "$TESTS/save_manager_tests.cpp" "$GAME/save_manager.cpp" \
           "$GAME/dkr_save_codec.cpp" "$HERE/../fileio.cpp" "$HERE/../threading.cpp" \
           -lpthread
    echo "  -- save_manager, $label --"
    ( cd "$tmp" && "$tmp/save_manager" )
  done
fi

# --- E04-S08: the reference software rasteriser -------------------------------
#
# The rendering oracle. Every check compares a read-back pixel against an
# analytically computed value, because an oracle checked by eye is not an oracle:
# its entire value rests on the trust placed in it, and "it looks right" does not
# transfer.
#
# The same binary built for the target runs on the machine, and the two render
# the same bytes.
if [[ "$suite" == "all" || "$suite" == "render" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "error: no host C compiler ($CC)" >&2; exit 2; }
  R="$HERE/../../render"
  "$CC" -std=gnu11 -O2 -Wall -Wextra -I"$HERE/../.." -I"$R" \
        -o "$tmp/test_software" "$R/tests/test_software.c" "$R/software.c"
  echo
  ( cd "$tmp" && "$tmp/test_software" )
fi

# --- E04-S06: RDP state decoding ----------------------------------------------
#
# The vectors come from the decompilation's headers, resolved by
# tools/win95/gen_combiner_vectors.py. A transcription by hand would go wrong in
# silence, and the test would then share the error of the code it checks.
if [[ "$suite" == "all" || "$suite" == "rdp" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "error: no host C compiler ($CC)" >&2; exit 2; }
  R="$HERE/../../render"
  "$CC" -std=gnu11 -O2 -Wall -Wextra -I"$HERE/../.." -I"$R" -I"$R/tests" \
        -o "$tmp/test_rdp_state" "$R/tests/test_rdp_state.c" "$R/rdp_state.c"
  echo
  ( cd "$tmp" && "$tmp/test_rdp_state" )
fi

# --- E04-S02: the display-list decoder ----------------------------------------
#
# By injecting deliberately corrupted lists. That is what makes the suite
# possible without the ROM: a corrupt list is written, a real one is captured.
if [[ "$suite" == "all" || "$suite" == "texture" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "error: no host C compiler ($CC)" >&2; exit 2; }
  R="$HERE/../../render"
  "$CC" -std=gnu11 -O2 -Wall -Wextra -I"$HERE/../.." -I"$R" \
        -o "$tmp/test_texture" "$R/tests/test_texture.c" "$R/texture.c"
  echo
  ( cd "$tmp" && "$tmp/test_texture" )
fi

if [[ "$suite" == "all" || "$suite" == "f3ddkr" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "error: no host C compiler ($CC)" >&2; exit 2; }
  R="$HERE/../../render"
  "$CC" -std=gnu11 -O2 -Wall -Wextra -I"$HERE/../.." -I"$R" \
        -o "$tmp/test_f3ddkr" "$R/tests/test_f3ddkr.c" "$R/f3ddkr.c" \
        "$R/clip.c" "$R/transform.c" "$R/rdp_state.c" "$R/combiner.c" "$R/texture.c"
  echo
  ( cd "$tmp" && "$tmp/test_f3ddkr" )
fi

# --- E04-S03: vertex transformation -------------------------------------------
#
# Correctness is checked here; **the figure that counts is measured on the
# target**. On the host the measurement loop is skipped: a Ryzen says nothing
# about a Pentium II at 400 MHz.
if [[ "$suite" == "all" || "$suite" == "transform" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "error: no host C compiler ($CC)" >&2; exit 2; }
  R="$HERE/../../render"
  "$CC" -std=gnu11 -O2 -Wall -Wextra -I"$HERE/../.." -I"$R" \
        -o "$tmp/test_transform" "$R/tests/test_transform.c" "$R/transform.c"
  echo
  ( cd "$tmp" && "$tmp/test_transform" )
fi

# --- E04-S05: clipping, back faces, scissor -----------------------------------
#
# Clipping is a classic source of subtle errors: a badly clipped triangle
# produces a shard of geometry crossing the screen, very visible and hard to
# reproduce because it depends on one precise angle. So the suite places the
# camera **inside** the geometry, and checks each interpolated attribute
# separately.
if [[ "$suite" == "all" || "$suite" == "clip" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "error: no host C compiler ($CC)" >&2; exit 2; }
  R="$HERE/../../render"
  "$CC" -std=gnu11 -O2 -Wall -Wextra -I"$HERE/../.." -I"$R" \
        -o "$tmp/test_clip" "$R/tests/test_clip.c" "$R/clip.c" "$R/transform.c"
  echo
  ( cd "$tmp" && "$tmp/test_clip" )
fi

# --- E04: the complete chain on a synthetic scene -----------------------------
#
# Five modules fit together. What is established is not that the rendering is
# right - the game will be needed for that - but that the chain is **continuous**:
# a command written in RDRAM comes back out as pixels, and every stage hands its
# neighbour what it expects.
if [[ "$suite" == "all" || "$suite" == "pipeline" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "error: no host C compiler ($CC)" >&2; exit 2; }
  R="$HERE/../../render"
  "$CC" -std=gnu11 -O2 -Wall -Wextra -I"$HERE/../.." -I"$R" \
        -o "$tmp/test_pipeline" "$R/tests/test_pipeline.c" "$R/f3ddkr.c" \
        "$R/clip.c" "$R/transform.c" "$R/software.c" "$R/rdp_state.c" \
        "$R/combiner.c" "$R/texture.c"
  echo
  ( cd "$tmp" && "$tmp/test_pipeline" )
fi

# The TMU allocator. It does not talk to Glide - the transfer goes through a
# function pointer - and is therefore tested entirely on the host. That is
# necessary: the missing ROM forbids checking it in the game, and the card
# returns no error code for what it receives.
# The combiner: the oracle, checked against values computed by hand, and the
# mapping table, checked by properties - no duplicate key, every entry findable,
# every category justified.
if [[ "$suite" == "all" || "$suite" == "combiner" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "error: no host C compiler ($CC)" >&2; exit 2; }
  R="$HERE/../../render"
  "$CC" -std=gnu11 -O2 -Wall -Wextra -I"$HERE/../.." -I"$R" \
        -o "$tmp/test_combiner" "$R/tests/test_combiner.c" "$R/combiner.c" \
        "$R/rdp_state.c"
  echo
  ( cd "$tmp" && "$tmp/test_combiner" )
fi

if [[ "$suite" == "all" || "$suite" == "tmu" ]]; then
  command -v "$CC" >/dev/null \
    || { echo "error: no host C compiler ($CC)" >&2; exit 2; }
  R="$HERE/../../render"
  "$CC" -std=gnu11 -O2 -Wall -Wextra -I"$HERE/../.." -I"$R" \
        -o "$tmp/test_tmu" "$R/tests/test_tmu.c" "$R/tmu.c"
  echo
  ( cd "$tmp" && "$tmp/test_tmu" )
fi
