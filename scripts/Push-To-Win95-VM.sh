#!/usr/bin/env bash
# E09-S01 - copies files from the host to the test machine's transfer disk, which
# appears as D: under Windows 95.
#
#   scripts/Push-To-Win95-VM.sh build/DKR-R.exe
#   scripts/Push-To-Win95-VM.sh --dir BUILD build/*.exe build/*.dll
#   scripts/Push-To-Win95-VM.sh --clear-dirty      see below
#
# Goes through mtools: no privilege elevation, and the machine does not need to be
# stopped for the write to succeed - but it does need to be for the guest to see
# the result, Windows 95 caching the volume.
#
# ## The dirty volume, and why this script names it
#
# Windows 95 clears bit 15 of FAT entry 1 while a volume is mounted and sets it
# again on a clean dismount. A crash therefore leaves it cleared, and on this
# target crashes are the normal case - E02-S05 provokes power cuts on purpose,
# and the game has been faulting for most of its bring-up. `AutoScan=0` in
# MSDOS.SYS, which E09-S01 set so that ScanDisk stops swallowing the keystrokes
# meant for the desktop, means nothing ever clears the flag again either.
#
# mtools refuses a volume flagged dirty, and says `Error reading FAT`. That
# message names neither the cause nor the remedy, and it is indistinguishable
# from a genuinely corrupt image - the kind of mute failure this repository keeps
# a record of. So the flag is checked here, before mtools is reached.
#
# `--clear-dirty` clears it, and refuses to do so blind: it walks the directory
# tree first and reports lost and cross-linked clusters. Lost clusters are the
# harmless residue of an interrupted write; a cross-link is real corruption, and
# there the answer is to restore the image rather than to clear a flag.
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

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FATTOOL="$HERE/tools/win95/fat_volume.py"

if [[ "${1:-}" == "--clear-dirty" ]]; then
  python3 "$FATTOOL" "${IMG%%@@*}" --offset "$PART_OFFSET" --clear-dirty
  exit $?
fi

# The preflight. mtools would otherwise fail here with `Error reading FAT`, which
# says neither what is wrong nor what to do about it.
if python3 "$FATTOOL" "${IMG%%@@*}" --offset "$PART_OFFSET" \
     2>/dev/null | grep -q 'clean flag   : DIRTY'; then
  printf '\033[1;31merror:\033[0m the transfer disk is flagged dirty.\n' >&2
  printf '  Windows 95 did not dismount it - a crash, or the machine is still running.\n' >&2
  printf '  mtools refuses such a volume and reports only "Error reading FAT".\n\n' >&2
  printf '  Stop the machine, then inspect and clear:\n' >&2
  printf '    scripts/Drive-Win95-VM.sh stop\n' >&2
  printf '    scripts/Push-To-Win95-VM.sh --clear-dirty\n' >&2
  exit 1
fi

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
