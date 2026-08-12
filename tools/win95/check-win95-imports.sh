#!/usr/bin/env bash
# E00-S01 / E01-S04 — verifie qu'un binaire ne reclame que des symboles que
# Windows 95 offre reellement.
#
#   tools/win95/check-win95-imports.sh build/DKR-R.EXE
#   tools/win95/check-win95-imports.sh --refresh          (reconstruit la reference)
#
# La reference n'est pas une liste ecrite a la main : elle est extraite des DLL
# de la machine de test elle-meme (C:\WINDOWS\SYSTEM), ce qui la rend exacte
# pour *cette* installation plutot que pour une idee de Windows 95.
#
# Piege : plusieurs API existent dans la table d'exports sans rien faire. Toute
# la famille Unicode `...W` de KERNEL32 est un stub qui renvoie 0 et pose
# ERROR_CALL_NOT_IMPLEMENTED. Elles passent donc ce controle et echouent quand
# meme a l'execution — voir docs/research/win95-blockers.md. Le script les
# signale separement.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
VMDISK="${DKR_WIN95_DISK:-$PREFIX/vm/dkr-p2-voodoo2/win95.img}"
REF="$PREFIX/win95-exports.txt"
DLLS=(KERNEL32 USER32 ADVAPI32 GDI32 WINMM MSVCRT)

# mtools est installe dans le prefixe local, sans droits root.
export PATH="$PREFIX/bin:$PATH"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merreur:\033[0m %s\n' "$*" >&2; exit 1; }

build_reference() {
  command -v mcopy >/dev/null || die "mtools absent — voir scripts/Setup-Win95-TestVM.sh"
  [[ -f "$VMDISK" ]] || die "disque introuvable : $VMDISK"
  local tmp; tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' RETURN
  export MTOOLS_SKIP_CHECK=1
  # La partition commence au secteur 63 ; mtools doit viser le systeme de
  # fichiers, pas le debut de l'image.
  local part="$VMDISK@@32256"
  : > "$REF.tmp"
  for d in "${DLLS[@]}"; do
    if mcopy -n -i "$part" "::/WINDOWS/SYSTEM/$d.DLL" "$tmp/$d.DLL" 2>/dev/null; then
      python3 "$ROOT/tools/win95/pe_symbols.py" --exports "$tmp/$d.DLL" >> "$REF.tmp"
      say "$d.DLL lu"
    else
      say "$d.DLL absent de l'image — ignore"
    fi
  done
  sort -u "$REF.tmp" > "$REF"; rm -f "$REF.tmp"
  say "reference : $(wc -l < "$REF") symboles -> $REF"
}

[[ "${1:-}" == "--refresh" ]] && { build_reference; exit 0; }
[[ $# -ge 1 ]] || die "usage: $(basename "$0") <binaire.exe> | --refresh"
[[ -f "$REF" ]] || build_reference

status=0
for bin in "$@"; do
  [[ -f "$bin" ]] || die "introuvable : $bin"
  imports="$(python3 "$ROOT/tools/win95/pe_symbols.py" --imports "$bin")"
  syms="$(cut -f2 <<< "$imports" | sort -u)"
  missing="$(comm -23 <(echo "$syms") "$REF" || true)"

  echo "### $bin — $(wc -l <<< "$syms") symboles importes"
  if [[ -n "$missing" ]]; then
    status=1
    while read -r s; do
      [[ -z "$s" ]] && continue
      printf '  \033[1;31mABSENT\033[0m  %-34s <- %s\n' \
        "$s" "$(grep -P "\t\Q$s\E$" <<< "$imports" | cut -f1 | tr '\n' ' ')"
    done <<< "$missing"
  else
    printf '  \033[1;32maucun symbole manquant\033[0m\n'
  fi

  stubs="$(grep -E '^KERNEL32\.DLL\t\w+W$' <<< "$imports" | cut -f2 || true)"
  if [[ -n "$stubs" ]]; then
    printf '  \033[1;33mSTUB\033[0m    %s\n' "$(tr '\n' ' ' <<< "$stubs")"
    printf '          (exportees mais inoperantes sous 9x : basculer sur ...A)\n'
  fi
done
exit $status
