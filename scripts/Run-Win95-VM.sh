#!/usr/bin/env bash
# E09-S01 — Lance la machine de test Windows 95 / 3dfx.
#
#   scripts/Run-Win95-VM.sh                          demarre la machine
#   scripts/Run-Win95-VM.sh --cdrom win95.iso        avec un media dans le lecteur
#   scripts/Run-Win95-VM.sh --snapshot save          fige l'etat courant en reference
#   scripts/Run-Win95-VM.sh --restore                revient a l'etat de reference
#
# L'instantane est ce qui rend cet environnement utilisable : un plantage de
# Glide en plein ecran peut laisser l'invite dans un etat irrecuperable, et sans
# restauration rapide chaque essai rate couterait une reinstallation.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
VM_NAME="${DKR_WIN95_VM:-dkr-p2-voodoo2}"
VM="$PREFIX/vm/$VM_NAME"
SNAP="$VM/.reference"
BOX="$PREFIX/opt/86box/squashfs-root/AppRun"

die() { printf '\033[1;31merreur:\033[0m %s\n' "$*" >&2; exit 1; }
say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

[[ -x "$BOX" ]] || die "86Box absent. Lancez d'abord scripts/Setup-Win95-TestVM.sh"
[[ -f "$VM/86box.cfg" ]] || die "Machine absente. Lancez d'abord scripts/Setup-Win95-TestVM.sh"

cdrom=""
action="run"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cdrom)    cdrom="$(readlink -f "$2")"; shift 2 ;;
    --snapshot) action="snapshot"; shift ;;
    --restore)  action="restore"; shift ;;
    -h|--help)  sed -n '2,12p' "$0"; exit 0 ;;
    *) die "option inconnue : $1" ;;
  esac
done

case "$action" in
  snapshot)
    say "Gel de l'etat courant comme reference"
    rm -rf "$SNAP"; mkdir -p "$SNAP"
    cp -a "$VM/win95.img" "$VM/86box.cfg" "$SNAP/"
    [[ -d "$VM/nvr" ]] && cp -a "$VM/nvr" "$SNAP/"
    say "Reference ecrite dans $SNAP"
    exit 0 ;;
  restore)
    [[ -d "$SNAP" ]] || die "aucune reference ; creez-en une avec --snapshot"
    say "Restauration de l'etat de reference"
    cp -a "$SNAP/win95.img" "$VM/win95.img"
    cp -a "$SNAP/86box.cfg" "$VM/86box.cfg"
    [[ -d "$SNAP/nvr" ]] && { rm -rf "$VM/nvr"; cp -a "$SNAP/nvr" "$VM/"; }
    say "Machine revenue a la reference (le disque de transfert n'est pas touche)"
    exit 0 ;;
esac

if [[ -n "$cdrom" ]]; then
  [[ -f "$cdrom" ]] || die "media introuvable : $cdrom"
  say "Media insere : $cdrom"
  python3 - "$VM/86box.cfg" "$cdrom" <<'PY'
import re, sys, pathlib
cfg, iso = pathlib.Path(sys.argv[1]), sys.argv[2]
text = cfg.read_text()
text = re.sub(r'^cdrom_01_image_path\s*=.*$', f'cdrom_01_image_path = {iso}', text, flags=re.M)
cfg.write_text(text)
PY
fi

say "Demarrage de $VM_NAME"
exec "$BOX" -R "$PREFIX/opt/86box/roms" -P "$VM" -N "$@"
