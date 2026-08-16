#!/usr/bin/env bash
# E09-S01 - starts the Windows 95 / 3dfx test machine.
#
#   scripts/Run-Win95-VM.sh                          starts the machine
#   scripts/Run-Win95-VM.sh --cdrom win95.iso        with a medium in the drive
#   scripts/Run-Win95-VM.sh --snapshot save          freezes the current state as the reference
#   scripts/Run-Win95-VM.sh --restore                returns to the reference state
#
# The snapshot is what makes this environment usable: a full-screen Glide crash can
# leave the guest in an unrecoverable state, and without a quick restore every
# failed attempt would cost a reinstallation.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
VM_NAME="${DKR_WIN95_VM:-dkr-p2-voodoo2}"
VM="$PREFIX/vm/$VM_NAME"
SNAP="$VM/.reference"
BOX="$PREFIX/opt/86box/squashfs-root/AppRun"

die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

[[ -x "$BOX" ]] || die "86Box absent. Run scripts/Setup-Win95-TestVM.sh first"
[[ -f "$VM/86box.cfg" ]] || die "Machine absent. Run scripts/Setup-Win95-TestVM.sh first"

cdrom=""
action="run"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cdrom)    cdrom="$(readlink -f "$2")"; shift 2 ;;
    --snapshot) action="snapshot"; shift ;;
    --restore)  action="restore"; shift ;;
    -h|--help)  sed -n '2,12p' "$0"; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

case "$action" in
  snapshot)
    say "Freezing the current state as the reference"
    rm -rf "$SNAP"; mkdir -p "$SNAP"
    cp -a "$VM/win95.img" "$VM/86box.cfg" "$SNAP/"
    [[ -d "$VM/nvr" ]] && cp -a "$VM/nvr" "$SNAP/"
    say "Reference written to $SNAP"
    exit 0 ;;
  restore)
    [[ -d "$SNAP" ]] || die "no reference; create one with --snapshot"
    say "Restoring the reference state"
    cp -a "$SNAP/win95.img" "$VM/win95.img"
    cp -a "$SNAP/86box.cfg" "$VM/86box.cfg"
    [[ -d "$SNAP/nvr" ]] && { rm -rf "$VM/nvr"; cp -a "$SNAP/nvr" "$VM/"; }
    say "Machine returned to the reference (the transfer disk is untouched)"
    exit 0 ;;
esac

if [[ -n "$cdrom" ]]; then
  [[ -f "$cdrom" ]] || die "medium not found: $cdrom"
  say "Medium inserted: $cdrom"
  python3 - "$VM/86box.cfg" "$cdrom" <<'PY'
import re, sys, pathlib
cfg, iso = pathlib.Path(sys.argv[1]), sys.argv[2]
text = cfg.read_text()
text = re.sub(r'^cdrom_01_image_path\s*=.*$', f'cdrom_01_image_path = {iso}', text, flags=re.M)
cfg.write_text(text)
PY
fi

say "Starting $VM_NAME"
exec "$BOX" -R "$PREFIX/opt/86box/roms" -P "$VM" -N "$@"
