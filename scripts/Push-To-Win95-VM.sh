#!/usr/bin/env bash
# E09-S01 — Copie des fichiers de l'hote vers le disque de transfert de la
# machine de test, qui apparait en D: sous Windows 95.
#
#   scripts/Push-To-Win95-VM.sh build/DKR-R.exe
#   scripts/Push-To-Win95-VM.sh --dir BUILD build/*.exe build/*.dll
#
# Passe par mtools : aucune elevation de privileges, et la machine n'a pas
# besoin d'etre arretee pour que l'ecriture reussisse — mais elle doit l'etre
# pour que l'invite voie le resultat, Windows 95 mettant le volume en cache.
set -euo pipefail

PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
VM_NAME="${DKR_WIN95_VM:-dkr-p2-voodoo2}"
# Le disque porte une table de partition : mtools doit viser le systeme de
# fichiers, qui commence au secteur 63, et non le debut de l'image.
PART_OFFSET=$((63 * 512))
IMG="$PREFIX/vm/$VM_NAME/transfer.img@@$PART_OFFSET"
MCOPY="$PREFIX/bin/mcopy"
MMD="$PREFIX/bin/mmd"
MDIR="$PREFIX/bin/mdir"

die() { printf '\033[1;31merreur:\033[0m %s\n' "$*" >&2; exit 1; }
say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

[[ -f "${IMG%%@@*}" ]] || die "disque de transfert absent : ${IMG%%@@*}"
[[ -x "$MCOPY" ]] || die "mtools absent. Lancez scripts/Setup-Win95-TestVM.sh"

subdir=""
if [[ "${1:-}" == "--dir" ]]; then subdir="$2"; shift 2; fi
[[ $# -gt 0 ]] || die "usage: $0 [--dir SOUSDOSSIER] fichier..."

target="::"
if [[ -n "$subdir" ]]; then
  # FAT16 : les noms tiennent en 8.3, majuscules.
  [[ "$subdir" =~ ^[A-Za-z0-9_-]{1,8}$ ]] || die "sous-dossier non conforme 8.3 : $subdir"
  subdir="${subdir^^}"
  "$MMD" -i "$IMG" "::/$subdir" 2>/dev/null || true
  target="::/$subdir"
fi

for f in "$@"; do
  [[ -f "$f" ]] || die "fichier introuvable : $f"
  base="$(basename "$f")"
  name="${base%.*}"; ext="${base##*.}"
  if (( ${#name} > 8 )) || { [[ "$base" == *.* ]] && (( ${#ext} > 3 )); }; then
    printf '\033[1;33mattention:\033[0m %s sera tronque par FAT16 en 8.3\n' "$base"
  fi
  "$MCOPY" -i "$IMG" -o "$f" "$target/" || die "echec de la copie de $f"
  say "copie : $base -> D:${subdir:+\\$subdir}"
done

say "Contenu du disque de transfert"
"$MDIR" -i "$IMG" "$target"
