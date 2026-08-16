#!/usr/bin/env bash
# E01-S01 - builds the Windows 95 / 3dfx Voodoo target.
#
#   ./Build-Win95.sh
#   DKR_WIN95_BUILD_DIR=/tmp/w95 ./Build-Win95.sh
#
# Modelled on Build-Linux.sh, with the same prerequisite checks at the head of the
# script: on this target, a missing tool otherwise shows up as an obscure
# compilation error a minute later.
#
# What this script builds today: the compatibility bridge and the witness. The
# game itself is not compilable for this target yet - `ultramodern` and
# `librecomp` are waiting on E01-S02 and E01-S03, the recompiled code on
# E01-S05.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${DKR_WIN95_BUILD_DIR:-${project_root}/build/win95}"
prefix="${DKR_WIN95_PREFIX:-${HOME}/.local/dkr-win95}"
export PATH="${prefix}/bin:${prefix}/opt/mingw/usr/bin:${PATH}"

fail() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

# --- Prerequisites -----------------------------------------------------------

command -v cmake >/dev/null || fail \
  "cmake is absent. Install it without privileges with scripts/Setup-Win95-Toolchain.sh"
command -v ninja >/dev/null || fail \
  "ninja is absent. Install it without privileges with scripts/Setup-Win95-Toolchain.sh"

for tool in i686-w64-mingw32-gcc-posix i686-w64-mingw32-g++-posix i686-w64-mingw32-objdump; do
  command -v "$tool" >/dev/null || fail \
    "${tool} is absent. The target requires mingw-w64 i686, posix threading model:
    apt-get download g++-mingw-w64-i686-posix gcc-mingw-w64-i686-posix \\
                     gcc-mingw-w64-i686-posix-runtime
    then dpkg-deb -x each package into ${prefix}/opt/mingw/
  The -posix suffix is not optional: see docs/adr/0001-toolchain.md."
done

[[ -f "${project_root}/cmake/toolchain-win95.cmake" ]] || fail \
  "cmake/toolchain-win95.cmake is absent."
[[ -x "${project_root}/tools/win95/check-instruction-set.sh" ]] || fail \
  "tools/win95/check-instruction-set.sh is absent or not executable."

# The verifier is put to the test before being used: a broken verifier and a
# satisfied one keep quiet in the same way.
say "Trial of the instruction-set verifier"
"${project_root}/tools/win95/check-instruction-set.sh" --self-test >/dev/null \
  || fail "the instruction-set verifier does not detect the injected SSE."

# Same reason for the import check, and one more since E02-S01: it must refuse two
# things of different natures - an *absent* symbol, whose absence is loud, and a
# symbol *exported but empty*, whose failure is silent. The second is the one that
# had escaped everyone.
say "Trial of the import and stub checks"
python3 "${project_root}/tools/win95/check_imports.py" --self-test >/dev/null \
  || fail "the import check does not detect the dirty witness or the hollow one."

# --- Configuration and compilation -------------------------------------------

say "Configuration (${build_dir})"
cmake -S "${project_root}/runtime-recomp" -B "${build_dir}" -G Ninja \
  --toolchain "${project_root}/cmake/toolchain-win95.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DDKRPORT_ROOT="${project_root}" \
  -DDKR_RUNTIME_TARGET_WIN95=ON

say "Building"
cmake --build "${build_dir}" --parallel

# --- Output checks -----------------------------------------------------------
#
# The instruction-set check has already run, as the target's post-link step. What
# remains is the import check: under Windows 95 the loader resolves every import at
# startup, so a missing symbol is fatal even if the function is never called - and
# nothing flags it at link time.

witness="${build_dir}/bin/WITNESS.EXE"
[[ -f "${witness}" ]] || fail "witness absent after the build: ${witness}"

# Both checks have already run as each target's post-link step; we replay them here
# on the final binary, because that is the one which will be copied to the machine
# and because the script must be runnable on an existing build.
say "Checking the imports against Windows 95's real exports"
python3 "${project_root}/tools/win95/check_imports.py" \
        --objects "${build_dir}" "${witness}" \
  || fail "the witness requires symbols absent from Windows 95."

printf '\n\033[1;32mWindows 95 target built.\033[0m\n'
printf 'Witness: %s\n' "${witness}"
printf 'To run on the test machine:\n'
printf '  scripts/Push-To-Win95-VM.sh %s\n' "${witness}"
