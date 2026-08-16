#!/usr/bin/env bash
# E09-S01 - copies files from the host to the test machine's transfer disk, which
# appears as D: under Windows 95.
#
#   scripts/Push-To-Win95-VM.sh build/DKR-R.exe
#   scripts/Push-To-Win95-VM.sh --dir BUILD build/*.exe build/*.dll
#
# Goes through mtools: no privilege elevation, and the machine does not need to be
# stopped for the write to succeed - but it does need to be for the guest to see
# the result, Windows 95 caching the volume.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
VM_NAME="${DKR_WIN95_VM:-dkr-p2-voodoo2}"
# The disk carries a partition table: mtools must aim at the file system, which
# starts at sector 63, and not at the start of the image.
PART_OFFSET=$((63 * 512))
IMG="$PREFIX/vm/$VM_NAME/transfer.img@@$PART_OFFSET"
MCOPY="$PREFIX/bin/mcopy"
MMD="$PREFIX/bin/mmd"
MDIR="$PREFIX/bin/mdir"

die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

[[ -f "${IMG%%@@*}" ]] || die "transfer disk absent: ${IMG%%@@*}"
[[ -x "$MCOPY" ]] || die "mtools absent. Run scripts/Setup-Win95-TestVM.sh"

subdir=""
if [[ "${1:-}" == "--dir" ]]; then subdir="$2"; shift 2; fi
[[ $# -gt 0 ]] || die "usage: $0 [--dir SUBDIR] file..."

target="::"
if [[ -n "$subdir" ]]; then
  # FAT16: the names fit in 8.3, upper case.
  [[ "$subdir" =~ ^[A-Za-z0-9_-]{1,8}$ ]] || die "subdirectory not 8.3-conformant: $subdir"
  subdir="${subdir^^}"
  "$MMD" -i "$IMG" "::/$subdir" 2>/dev/null || true
  target="::/$subdir"
fi

for f in "$@"; do
  [[ -f "$f" ]] || die "file not found: $f"
  base="$(basename "$f")"
  name="${base%.*}"; ext="${base##*.}"
  if (( ${#name} > 8 )) || { [[ "$base" == *.* ]] && (( ${#ext} > 3 )); }; then
    printf '\033[1;33mwarning:\033[0m %s will be truncated to 8.3 by FAT16\n' "$base"
  fi
  "$MCOPY" -i "$IMG" -o "$f" "$target/" || die "failed to copy $f"
  say "copied: $base -> D:${subdir:+\\$subdir}"
done

say "Contents of the transfer disk"
"$MDIR" -i "$IMG" "$target"
