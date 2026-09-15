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
#   scripts/Drive-Win95-VM.sh hold a 400           holds one key, in milliseconds
#   scripts/Drive-Win95-VM.sh pad left a start     the game's controls, by what
#                                                  they do -- use this one
#   scripts/Drive-Win95-VM.sh pad-hold 1200 a right   the same names, held down,
#                                                  several at once -- for driving
#   scripts/Drive-Win95-VM.sh type "E:\WIN95\INSTALL.EXE"
#   scripts/Drive-Win95-VM.sh run "D:\DKRR.EXE D:\DKR.Z64"   starts the game
#   scripts/Drive-Win95-VM.sh run-glide "D:\REPLAY.EXE ..."  starts it, checks
#                                                  that it really started, and
#                                                  returns when it has finished --
#                                                  use this whenever the output
#                                                  file is read back afterwards
#
# The game **requires the ROM path as an argument**; without it, it stops on "The
# diagnostic runtime requires a ROM path" and closes its window, which looks
# exactly like a silent crash. The ROM is on D:.
#   scripts/Drive-Win95-VM.sh grab                 gives the machine X focus
#   scripts/Drive-Win95-VM.sh grab click           ... and clicks into the guest.
#                                                  NEVER while the game is up: it
#                                                  drops the Voodoo full screen
#                                                  and the run does not recover.
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

# **The one mapping**, used by `pad` and by `pad-hold`. It was written out twice
# for about a minute, which is how a table that has already cost an afternoon
# starts to drift: the layout note below applies to every caller, and a second
# copy is a second place to forget it.
#
# The guest's layout is AZERTY and the host sends scancodes. `a` -- the port's
# stick-left -- arrives at the guest as `Q`, which is bound to L. See the comment
# on `pad` for what that cost.
pad_key() {
  case "$1" in
    left)  printf q ;;          # guest 'A' -- stick left
    right) printf d ;;          # 'D' on both layouts
    up)    printf z ;;          # guest 'W' -- stick up
    down)  printf s ;;          # 'S' on both layouts
    a)     printf space ;;
    b)     printf shift ;;
    z)     printf w ;;          # guest 'Z' -- the Z button
    start) printf Return ;;
    l)     printf a ;;          # guest 'Q' -- the L button
    r)     printf e ;;          # 'E' on both layouts
    *)     die "unknown control '$1'" ;;
  esac
}

# Is the captured screen the Glide one? Three points, so that a dialog in the
# middle of a black desktop cannot pass for a full-screen program.
screen_is() {
  python3 - "$1" "$2" <<'PY'
import sys
try:
    from PIL import Image
except ImportError:
    sys.exit(2)
im = Image.open(sys.argv[1]).convert("RGB")
pts = [(330, 300), (500, 200), (100, 450)]
black = all(sum(im.getpixel(p)) < 24 for p in pts)
sys.exit(0 if (black if sys.argv[2] == "black" else not black) else 1)
PY
}

need_running() {
  [[ -n "$(box_pids)" ]] || die "the machine is not started (scripts/Drive-Win95-VM.sh start)"
}

case "${1:-}" in
  start)
    [[ -x "$XVFB" ]] || die "Xvfb absent under $PREFIX/opt/xvfb"
    [[ -x "$XDO" ]]  || die "xdotool absent under $PREFIX/bin"
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
    # Here the click is right: nothing is running in the guest but the desktop,
    # and 86Box needs one to capture input at all. See `grab`.
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
          "$0" grab click >/dev/null 2>&1
          say "machine started, ScanDisk cleared in $attempt round(s)"
          exit 0 ;;
      esac
      "$0" grab click >/dev/null 2>&1
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
  run-glide)
    # Launches a Glide program and **checks that it started**, which `run` cannot.
    #
    # `run` walks the Start menu blind: Ctrl+Esc, three Ups, Return. That works
    # until Ctrl+Esc opens the task list instead of the Start menu - which this
    # guest does intermittently - and then the three Ups walk the task list and
    # Return opens whatever was under them. The measurement that follows reads
    # the *previous* run's output file and is wrong in a way nothing announces:
    # it happened three times on 14 September 2026, once producing a figure that
    # was briefly attributed to a code change that had in fact been reverted.
    #
    # A Glide program takes the whole screen, so "did it start" is one pixel.
    # This waits for the screen to go black, retries the launch if it does not -
    # clearing any window the failed attempt left open - and then waits for the
    # desktop to come back, so the caller knows the output file is this run's.
    #
    # **Only for a program that takes the screen at once.** `REPLAY.EXE --both`
    # does not: it rasterises the whole scene in software first, minutes of it,
    # with the desktop still showing, and only then opens the card. Watched from
    # here that reads as a launch that never happened, and the retry fires while
    # the program is working. For those, watch the artefact instead - poll the
    # output file's directory entry until it changes - which is what
    # `docs/TEST-ENVIRONMENT.md` shows and what the corpus sweep does.
    need_running; shift
    [[ $# -gt 0 ]] || die "usage: run-glide <command line> [seconds to wait]"
    cmd="$1"; budget="${2:-900}"
    tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
    # **Back to a bare desktop first, and between attempts.**
    #
    # The failure this guards against leaves the Start menu *open*, and `run`
    # begins with Ctrl+Esc - which closes an open menu instead of opening one, so
    # the three Ups and the Return then go nowhere and the next attempt opens it
    # again. Retrying without clearing oscillates for as many attempts as it is
    # given, which is what it did on 15 September 2026. Escape closes a menu;
    # Alt+F4 closes a window the stray keystrokes opened, and raises the shutdown
    # box when there is none, which the last Escape dismisses.
    # **Escape only, and never a blind Return.**
    #
    # The first version of this recovery pressed Return "in case it was
    # swallowed", and on 15 September 2026 it found the shutdown box with
    # "Arreter l'ordinateur" selected and switched the machine off in the middle
    # of a corpus sweep. Alt+F4 raises that box whenever the desktop has the
    # focus, so the two together are a power switch. Escape closes a menu and
    # dismisses a dialog, and does nothing anywhere else - which is the whole of
    # what a recovery should be allowed to do.
    clear_desktop() {
      "$0" key Escape >/dev/null 2>&1
      "$0" key Escape >/dev/null 2>&1
      "$0" key Escape >/dev/null 2>&1
    }
    started=0
    for attempt in 1 2 3 4; do
      clear_desktop
      "$0" run "$cmd" >/dev/null 2>&1
      for _ in $(seq 1 12); do
        sleep 5
        "$0" shot "$tmp/s.png" >/dev/null 2>&1 || continue
        if screen_is "$tmp/s.png" black; then started=1; break; fi
      done
      [[ $started -eq 1 ]] && break
      say "attempt $attempt: the program did not take the screen - clearing and retrying"
    done
    [[ $started -eq 1 ]] || die "the program never took the screen: $cmd"
    say "running: $cmd"
    waited=0
    while (( waited < budget )); do
      sleep 10; waited=$((waited + 10))
      "$0" shot "$tmp/s.png" >/dev/null 2>&1 || continue
      if ! screen_is "$tmp/s.png" black; then
        say "finished after about ${waited}s"
        exit 0
      fi
    done
    die "still running after ${budget}s: $cmd"
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
    # **Do not click into a running game.** Measured on 13 September 2026: the
    # click below lands inside the guest, and while DKR holds the Voodoo's
    # full-screen pass-through that takes the focus away from it. The picture
    # drops to the Windows desktop, the game's window goes black, and it **does
    # not come back** -- clicking its taskbar button does nothing. The run is
    # over.
    #
    # That is what killed the navigated run earlier the same day, whose log
    # stopped at list 300 with a black window: `grab` had been called before the
    # keystrokes, out of habit, from a session where the game was not yet up.
    #
    # So the click is no longer the default. Focusing the X window is what the
    # keystrokes actually need; the click exists only to make 86Box capture input
    # the first time, which it needs once, at the desktop, before the game starts.
    # `grab click` asks for it explicitly.
    need_running; W="$(window)"
    DISPLAY="$DISP" "$XDO" windowfocus --sync "$W" 2>/dev/null || true
    if [[ "${2:-}" == "click" ]]; then
      DISPLAY="$DISP" "$XDO" mousemove --window "$W" 300 250 click 1
      say "keyboard and mouse captured by the machine (clicked)"
    else
      say "X focus given to the machine; no click sent into the guest"
    fi
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
  pad)
    # The game's controls by what they DO, not by which host key happens to
    # produce them.
    #
    # **The guest's layout is AZERTY and the host sends scancodes**, which this
    # file already says about `type` and did not say about `key`. The cost of that
    # gap, measured on 12 September 2026: `a` -- the port's stick-left -- arrives
    # at the guest as `Q`, which is bound to the L button, so the menu never moved
    # left. `d` is the same key on both layouts and worked. Four taps left moved
    # nothing, four taps right moved four, and a whole afternoon was spent calling
    # that intermittent and writing up a latch hypothesis for it.
    #
    # Nothing was intermittent. Two keys, one of them mistranslated.
    #
    # The mapping is `runtime_platform.cpp`'s: WASD is the analogue stick, SPACE
    # is A, SHIFT is B, RETURN is Start, Z is the Z button. DKR navigates its
    # menus with the **stick**, not the D-pad, which is why `left`/`right` below
    # are the stick and not the arrows.
    need_running; shift
    [[ $# -gt 0 ]] || die "usage: pad <left|right|up|down|a|b|z|start|l|r> [...]"
    for name in "$@"; do
      k="$(pad_key "$name")" || exit 1
      DISPLAY="$DISP" "$XDO" key --clearmodifiers "$k"
      sleep 0.3
    done
    say "pad: $*"
    ;;
  pad-hold)
    # **Several of the game's controls held down together, by name.**
    #
    # `hold` takes a host key, so holding the accelerator means knowing that the
    # port's A button is SPACE, and holding a turn means knowing that stick-left
    # is the host's `q` -- the AZERTY translation this file exists to keep people
    # out of. `pad-hold` takes the same names as `pad` and goes through the same
    # table.
    #
    # And it holds **more than one**, which is the part `hold` could not do at
    # all. Driving in this game is accelerate *and* steer: with one key at a time
    # the only way to take a corner is to alternate, and alternating gives a car
    # that lurches and a heading nobody chose. Four separate attempts to drive
    # to a landmark twenty seconds away went that way before this existed.
    #
    #   pad-hold 1200 a right      accelerate and turn right for 1.2 s
    #   pad-hold 400 a             a nudge forward
    #
    # Released in reverse order, and released even if the sleep is interrupted:
    # a key left down in the guest is an accelerator stuck on, and the next
    # command inherits it without any sign that it did.
    need_running; shift
    [[ $# -ge 2 ]] || die "usage: pad-hold <milliseconds> <control> [control...]"
    hold_ms="$1"; shift
    keys=()
    for name in "$@"; do keys+=("$(pad_key "$name")") || exit 1; done
    release() {
      for (( i=${#keys[@]}-1; i>=0; i-- )); do
        DISPLAY="$DISP" "$XDO" keyup --clearmodifiers "${keys[$i]}" 2>/dev/null || true
      done
    }
    trap release EXIT INT TERM
    for k in "${keys[@]}"; do
      DISPLAY="$DISP" "$XDO" keydown --clearmodifiers "$k"
    done
    sleep "$(awk -v m="$hold_ms" 'BEGIN{printf "%.3f", m/1000}')"
    release
    trap - EXIT INT TERM
    say "pad-hold: $* for ${hold_ms} ms"
    ;;
  hold)
    # Presses a key, waits, releases it. `key` above sends a press and a release
    # within milliseconds, and that is not the same input.
    #
    # **Why the difference matters here.** The game reads its controller once a
    # frame and a frame is 170 ms (E00-S03). E06-S01's latch exists so that a tap
    # shorter than that is still seen: `dkr_window_key_down` answers true while a
    # key is held *or* while it is latched since the last `dkr_window_latch_clear`.
    # But the poll clears the latch when it reads it, so a keystroke that exists
    # only as a latch is reported to exactly one poll -- and if two polls fall
    # between the keystroke and the game's own read, the first consumes it.
    #
    # **The asymmetry this was built to explain had another cause**, and the
    # hypothesis above is unproven and probably unnecessary: three taps right
    # moved three and four taps left moved none because the guest reads AZERTY
    # and the host sends scancodes -- `a` arrived as `Q`. See `pad` above.
    #
    # Kept because a held key is a real input a tap is not -- accelerating out of
    # a corner needs one -- and because the reasoning above still describes a race
    # the latch does not obviously close. It is no longer offered as the
    # explanation of anything measured.
    #
    # The default is 400 ms: two frames at the measured rate, so at least one poll
    # sees the key genuinely down.
    need_running; shift
    [[ $# -gt 0 ]] || die "usage: hold <key> [milliseconds]"
    hold_ms="${2:-400}"
    DISPLAY="$DISP" "$XDO" keydown --clearmodifiers "$1"
    sleep "$(awk -v m="$hold_ms" 'BEGIN{printf "%.3f", m/1000}')"
    DISPLAY="$DISP" "$XDO" keyup --clearmodifiers "$1"
    say "held: $1 for ${hold_ms} ms"
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
