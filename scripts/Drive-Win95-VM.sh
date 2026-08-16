#!/usr/bin/env bash
# E09-S01 - drives the test machine without a physical screen.
#
# Runs 86Box on a virtual X display, allows its screen to be captured and keys to
# be injected into it. That is what makes the environment usable from a machine
# with no graphical session, and it is the foundation of E09-S02's visual
# comparison harness.
#
#   scripts/Drive-Win95-VM.sh start                starts the machine
#   scripts/Drive-Win95-VM.sh shot screen.png      captures the screen
#   scripts/Drive-Win95-VM.sh key F1               sends one key
#   scripts/Drive-Win95-VM.sh type "E:\WIN95\INSTALL.EXE"
#   scripts/Drive-Win95-VM.sh run "D:\DKRR.EXE D:\DKR.Z64"   starts the game
#
# The game **requires the ROM path as an argument**; without it, it stops on "The
# diagnostic runtime requires a ROM path" and closes its window, which looks
# exactly like a silent crash. The ROM is on D:.
#   scripts/Drive-Win95-VM.sh grab                 captures keyboard and mouse
#   scripts/Drive-Win95-VM.sh stop
#
# Two traps discovered while building this, each costing an hour:
#
#  1. `xdotool key --window` goes through XSendEvent, which Qt ignores. One must
#     set the X focus and then use XTEST (`xdotool key` without --window).
#  2. 86Box only routes the keyboard to the emulated machine after a click in its
#     window, which captures the devices. Without that click, the keys go to the
#     emulator's interface and not to the guest.
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
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

box_pids() { ps -eo pid,args | grep -F 'local/bin/86Box' | grep -v grep | awk '{print $1}'; }
window() { DISPLAY="$DISP" "$XDO" search --name "86Box" 2>/dev/null | tail -1; }

need_running() {
  [[ -n "$(box_pids)" ]] || die "the machine is not started (scripts/Drive-Win95-VM.sh start)"
}

case "${1:-}" in
  start)
    [[ -x "$XVFB" ]] || die "Xvfb absent sous $PREFIX/opt/xvfb"
    [[ -x "$XDO" ]]  || die "xdotool absent sous $PREFIX/bin"
    [[ -f "$VM/86box.cfg" ]] || die "machine absent: $VM"
    # **One instance at a time.**
    #
    # Nothing stopped `start` from launching one more on an already-running
    # machine, and a development session calls `boot` on every attempt. Thirty-one
    # instances accumulated that way, all mounting the same disk image for writing.
    #
    # The symptom has nothing to do with the cause: it is `Push-To-Win95-VM.sh`
    # that fails, on "Error reading FAT", because the FAT mtools reads is the one
    # thirty other emulators are busy rewriting. One suspects the volume is dirty,
    # adds MTOOLS_SKIP_CHECK, and writes into an image corrupted by instances one
    # cannot see.
    #
    # The guard rail costs three lines. The hour lost searching elsewhere did not.
    if [[ -n "$(box_pids)" ]]; then
      say "a machine is already running (pid $(box_pids | tr '\n' ' ')) - nothing to do"
      exit 0
    fi
    if ! DISPLAY="$DISP" "$XDO" getactivewindow >/dev/null 2>&1 \
         && ! pgrep -f "Xvfb $DISP" >/dev/null 2>&1; then
      say "Starting the virtual display $DISP"
      LD_LIBRARY_PATH="$PREFIX/opt/xvfb/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}" \
        "$XVFB" "$DISP" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
      sleep 2
    fi
    say "Starting $VM_NAME on $DISP"
    DISPLAY="$DISP" QT_QPA_PLATFORM=xcb \
      "$BOX" -R "$PREFIX/opt/86box/roms" -P "$VM" -N >/dev/null 2>&1 &
    sleep 20
    W="$(window)"; [[ -n "$W" ]] || die "the 86Box window did not appear"
    say "Window $W ready. Capturing the keyboard:"
    DISPLAY="$DISP" "$XDO" windowfocus --sync "$W" 2>/dev/null || true
    DISPLAY="$DISP" "$XDO" mousemove --window "$W" 300 250 click 1 2>/dev/null || true
    say "The machine is running. `basename "$0"` shot / key / type / stop"
    ;;
  boot)
    # Starts and **walks through ScanDisk**, which appears every time the machine
    # stopped uncleanly - that is, on every crash of the game, hence often during a
    # development session.
    #
    # Without this step, the keystrokes meant for the desktop go into ScanDisk's
    # boxes and the next command runs into the void. The symptom is disconcerting:
    # the program "does not start" when it was never launched at all.
    #
    # The sequence is the one observed on this machine, in this order:
    #   Enter       starts the scan
    #   Enter       "lost data" - we abandon it
    #   Enter       "no floppy in drive A"
    #   Tab, Enter  "undo disk" - we choose Skip
    "$0" start >/dev/null 2>&1
    sleep 55
    # **The order of ScanDisk's boxes varies from one boot to the next**: depending
    # on what the previous crash left behind, there is or is not lost data, a
    # request for a floppy, an offer of an undo disk. A fixed sequence of keystrokes
    # works one time in two, which is worse than a clean failure - the next command
    # runs into the void.
    #
    # So we loop until the desktop is visible, sending on each round the two
    # gestures that advance any of these boxes: Enter, then Tab-Enter for those
    # whose default button is not the right one.
    tmp_shot="$(mktemp --suffix=.png)"
    for attempt in $(seq 1 20); do
      import -display "$DISP" -window root "$tmp_shot" 2>/dev/null || true
      colour="$(convert "$tmp_shot" -format "%[pixel:p{400,300}]" info: 2>/dev/null || echo "")"
      # This machine's desktop is turquoise; ScanDisk is blue and grey.
      case "$colour" in
        *"85,170,170"*|*"102,153,153"*)
          rm -f "$tmp_shot"
          "$0" grab >/dev/null 2>&1
          say "machine started, ScanDisk cleared in $attempt round(s)"
          exit 0 ;;
      esac
      "$0" grab >/dev/null 2>&1
      "$0" key Return >/dev/null 2>&1; sleep 4
      "$0" key Tab >/dev/null 2>&1; sleep 1
      "$0" key Return >/dev/null 2>&1; sleep 8
    done
    rm -f "$tmp_shot"
    die "the desktop did not appear after twenty rounds"
    ;;
  run)
    # Launches a program through the Run box and returns.
    need_running; shift
    "$0" grab >/dev/null 2>&1
    "$0" key ctrl+Escape >/dev/null 2>&1; sleep 3
    for i in 1 2 3; do "$0" key Up >/dev/null 2>&1; sleep 1; done
    "$0" key Return >/dev/null 2>&1; sleep 4
    "$0" type "$1" >/dev/null 2>&1; sleep 2
    "$0" key Return >/dev/null 2>&1
    say "launched: $1"
    ;;
  dismiss)
    # Closes a modal dialog by clicking its default button. Keystrokes are not
    # always enough: a system error box steals the focus without xdotool seeing
    # it.
    need_running; W="$(window)"
    DISPLAY="$DISP" "$XDO" windowactivate "$W" 2>/dev/null || true
    DISPLAY="$DISP" "$XDO" mousemove "${2:-495}" "${3:-272}" click 1 2>/dev/null || true
    say "box closed"
    ;;
  grab)
    need_running; W="$(window)"
    DISPLAY="$DISP" "$XDO" windowfocus --sync "$W" 2>/dev/null || true
    DISPLAY="$DISP" "$XDO" mousemove --window "$W" 300 250 click 1
    say "keyboard and mouse captured by the machine"
    ;;
  shot)
    need_running
    out="${2:-$VM/screenshot-$(date +%H%M%S).png}"
    command -v import >/dev/null || die "ImageMagick (import) is required"
    import -display "$DISP" -window root "$out"
    say "capture: $out"
    ;;
  key)
    need_running; shift
    [[ $# -gt 0 ]] || die "usage: key <key> [key...]"
    for k in "$@"; do DISPLAY="$DISP" "$XDO" key --clearmodifiers "$k"; sleep 0.3; done
    say "keys sent: $*"
    ;;
  type)
    # `xdotool type` sends scancodes, and the guest reads them with ITS OWN
    # keyboard layout - an AZERTY one here. Crossed letters and above all the
    # punctuation come out wrong: `D:\FSSEAM.EXE` becomes `DM"FSSEQ?:EXE`, which
    # gives a "file not found" box readily blamed on the binary.
    #
    # That is exactly what `tools/win95/azerty_keys.py` knows how to correct. It
    # already existed, but was not wired in here; it is now, because no Windows
    # path is written without a ":" or a "\".
    need_running
    [[ -n "${2:-}" ]] || die "usage: type <text>"
    mapfile -t _keys < <(python3 "$HERE/../tools/win95/azerty_keys.py" "$2" | tr ' ' '\n')
    for k in "${_keys[@]}"; do
      [[ -n "$k" ]] || continue
      DISPLAY="$DISP" "$XDO" key --clearmodifiers "$k"
      sleep 0.06
    done
    say "text typed: $2"
    ;;
  type-raw)
    # Without translation, for a guest whose layout is QWERTY.
    need_running
    [[ -n "${2:-}" ]] || die "usage: type-raw <text>"
    DISPLAY="$DISP" "$XDO" type --clearmodifiers --delay 60 -- "$2"
    say "text typed (raw)"
    ;;
  stop)
    n=0
    for p in $(box_pids); do kill -9 "$p" 2>/dev/null && n=$((n+1)); done
    say "$n 86Box process(es) stopped"
    ;;
  *)
    sed -n '2,19p' "$0"
    exit 2
    ;;
esac
