#!/usr/bin/env bash
# E09-S01 — Pilote la machine de test sans écran physique.
#
# Lance 86Box sur un affichage X virtuel, permet d'en capturer l'écran et d'y
# injecter des touches. C'est ce qui rend l'environnement utilisable depuis un
# poste sans session graphique, et c'est le socle du harnais de comparaison
# visuelle de E09-S02.
#
#   scripts/Drive-Win95-VM.sh start                démarre la machine
#   scripts/Drive-Win95-VM.sh shot ecran.png       capture l'écran
#   scripts/Drive-Win95-VM.sh key F1               envoie une touche
#   scripts/Drive-Win95-VM.sh type "E:\WIN95\INSTALL.EXE"
#   scripts/Drive-Win95-VM.sh grab                 capture le clavier/souris
#   scripts/Drive-Win95-VM.sh stop
#
# Deux pièges découverts en montant ceci, qui coûtent chacun une heure :
#
#  1. `xdotool key --window` passe par XSendEvent, que Qt ignore. Il faut poser
#     le focus X puis utiliser XTEST (`xdotool key` sans --window).
#  2. 86Box ne route le clavier vers la machine émulée qu'après un clic dans sa
#     fenêtre, qui capture les périphériques. Sans ce clic, les touches vont à
#     l'interface de l'émulateur et non à l'invité.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
VM_NAME="${DKR_WIN95_VM:-dkr-p2-voodoo2}"
VM="$PREFIX/vm/$VM_NAME"
DISP="${DKR_WIN95_DISPLAY:-:77}"
XVFB="$PREFIX/opt/xvfb/usr/bin/Xvfb"
XDO="$PREFIX/bin/xdotool"
BOX="$PREFIX/opt/86box/squashfs-root/AppRun"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merreur:\033[0m %s\n' "$*" >&2; exit 1; }

box_pids() { ps -eo pid,args | grep -F 'local/bin/86Box' | grep -v grep | awk '{print $1}'; }
window() { DISPLAY="$DISP" "$XDO" search --name "86Box" 2>/dev/null | tail -1; }

need_running() {
  [[ -n "$(box_pids)" ]] || die "la machine n'est pas démarrée (scripts/Drive-Win95-VM.sh start)"
}

case "${1:-}" in
  start)
    [[ -x "$XVFB" ]] || die "Xvfb absent sous $PREFIX/opt/xvfb"
    [[ -x "$XDO" ]]  || die "xdotool absent sous $PREFIX/bin"
    [[ -f "$VM/86box.cfg" ]] || die "machine absente : $VM"
    if ! DISPLAY="$DISP" "$XDO" getactivewindow >/dev/null 2>&1 \
         && ! pgrep -f "Xvfb $DISP" >/dev/null 2>&1; then
      say "Démarrage de l'affichage virtuel $DISP"
      LD_LIBRARY_PATH="$PREFIX/opt/xvfb/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}" \
        "$XVFB" "$DISP" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
      sleep 2
    fi
    say "Démarrage de $VM_NAME sur $DISP"
    DISPLAY="$DISP" QT_QPA_PLATFORM=xcb \
      "$BOX" -R "$PREFIX/opt/86box/roms" -P "$VM" -N >/dev/null 2>&1 &
    sleep 20
    W="$(window)"; [[ -n "$W" ]] || die "la fenêtre 86Box n'est pas apparue"
    say "Fenêtre $W prête. Capture du clavier :"
    DISPLAY="$DISP" "$XDO" windowfocus --sync "$W" 2>/dev/null || true
    DISPLAY="$DISP" "$XDO" mousemove --window "$W" 300 250 click 1 2>/dev/null || true
    say "La machine tourne. `basename "$0"` shot / key / type / stop"
    ;;
  grab)
    need_running; W="$(window)"
    DISPLAY="$DISP" "$XDO" windowfocus --sync "$W" 2>/dev/null || true
    DISPLAY="$DISP" "$XDO" mousemove --window "$W" 300 250 click 1
    say "clavier et souris capturés par la machine"
    ;;
  shot)
    need_running
    out="${2:-$VM/screenshot-$(date +%H%M%S).png}"
    command -v import >/dev/null || die "ImageMagick (import) est requis"
    import -display "$DISP" -window root "$out"
    say "capture : $out"
    ;;
  key)
    need_running; shift
    [[ $# -gt 0 ]] || die "usage: key <touche> [touche...]"
    for k in "$@"; do DISPLAY="$DISP" "$XDO" key --clearmodifiers "$k"; sleep 0.3; done
    say "touches envoyées : $*"
    ;;
  type)
    # `xdotool type` envoie des scancodes, et l'invité les interprète avec SA
    # disposition — française. Les lettres croisées et surtout la ponctuation en
    # ressortent fausses : `D:\FSSEAM.EXE` devient `DM"FSSEQ?:EXE`, ce qui donne
    # une boîte « fichier introuvable » qu'on impute volontiers au binaire.
    #
    # C'est exactement ce que `tools/win95/azerty_keys.py` sait corriger. Il
    # existait déjà, mais n'était pas branché ici ; il l'est maintenant, parce
    # qu'aucun chemin Windows ne s'écrit sans « : » ni « \ ».
    need_running
    [[ -n "${2:-}" ]] || die "usage: type <texte>"
    mapfile -t _keys < <(python3 "$HERE/../tools/win95/azerty_keys.py" "$2" | tr ' ' '\n')
    for k in "${_keys[@]}"; do
      [[ -n "$k" ]] || continue
      DISPLAY="$DISP" "$XDO" key --clearmodifiers "$k"
      sleep 0.06
    done
    say "texte saisi : $2"
    ;;
  type-raw)
    # Sans traduction, pour un invité dont la disposition serait qwerty.
    need_running
    [[ -n "${2:-}" ]] || die "usage: type-raw <texte>"
    DISPLAY="$DISP" "$XDO" type --clearmodifiers --delay 60 -- "$2"
    say "texte saisi (brut)"
    ;;
  stop)
    n=0
    for p in $(box_pids); do kill -9 "$p" 2>/dev/null && n=$((n+1)); done
    say "$n processus 86Box arrêté(s)"
    ;;
  *)
    sed -n '2,22p' "$0"
    exit 2
    ;;
esac
