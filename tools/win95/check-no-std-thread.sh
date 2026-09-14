#!/usr/bin/env bash
# The third guard rail: a standard-library thread in a linked Windows 95 binary.
#
#   tools/win95/check-no-std-thread.sh <binary>
#
# ## Why the other two do not catch this
#
# `check-instruction-set.sh` reads opcodes and `check_imports.py` reads the
# import table. A `std::thread` is neither: libstdc++ is linked statically here,
# so the thread machinery is *in* the binary and imports nothing Windows 95 is
# missing. The binary loads. It then dies the moment something constructs a
# thread:
#
#   terminate called after throwing an instance of 'std::system_error'
#     what():  Resource temporarily unavailable
#
# which is `pthread_create` failing behind the standard library. E02-S01's layer
# exists so that no project code reaches it, and `check-cpp-subset.py` watches
# the sources — but it matches text, and a translation unit can reach
# `std::thread` through a header it never names. `librecomp` does exactly that.
#
# So this one reads the **link**, which cannot be talked out of: if
# `std::thread::_M_start_thread` is in the binary, something calls it.
#
# ## What it costs when it fires
#
# A trip to the test machine, and a symptom that looks like anything. It has
# fired twice: once when the seam was first written, and again on 14 September
# 2026 when a dependency-patch rebase silently dropped five of its conversions.
# Neither time did anything else notice.
set -uo pipefail

binary="${1:-}"
if [[ -z "$binary" || ! -f "$binary" ]]; then
  printf 'usage: %s <binary>\n' "$0" >&2
  exit 2
fi

NM="${DKR_WIN95_NM:-i686-w64-mingw32-nm}"
if ! command -v "$NM" >/dev/null 2>&1; then
  # Not fatal: a host without the cross toolchain cannot have produced the
  # binary either, and refusing here would break a source-only checkout.
  printf '  \033[1;33mskipped\033[0m  %s: %s not found\n' "$(basename "$binary")" "$NM"
  exit 0
fi

hits="$("$NM" -C "$binary" 2>/dev/null | grep -E "std::thread::_M_start_thread|std::thread::thread" || true)"
if [[ -n "$hits" ]]; then
  printf '  \033[1;31mFORBIDDEN\033[0m  %s carries std::thread\n' "$(basename "$binary")"
  printf '%s\n' "$hits" | sed 's/^/            /'
  printf '            The seam is dkr::sync / recomp::sync (E02-S01, E02-S02).\n'
  printf '            See the last section of docs/WIN95-COMPAT.md.\n'
  exit 1
fi
printf '   %s: no std::thread\n' "$(basename "$binary")"
