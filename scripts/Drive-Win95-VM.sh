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
#   scripts/Drive-Win95-VM.sh pad-until 1500 a     presses until the screen
#                                                  actually moves -- walk a menu
#                                                  route with this, never with a
#                                                  fixed number of presses
#   scripts/Drive-Win95-VM.sh pad-until-screen start 90621
#                                                  presses until the screen's
#                                                  colour count matches a known
#                                                  fingerprint -- a window, never
#                                                  a floor
#   scripts/Drive-Win95-VM.sh game-mode            what the running game says it
#                                                  is doing: INTRO, MENU, INGAME.
#                                                  INGAME is a race
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

# **Both names.** The emulator is started through the AppImage's `AppRun`, which
# execs `.../squashfs-root/usr/local/bin/86Box`. Matching only the inner name
# leaves a window - between the fork and the exec - in which `start` sees no
# instance and launches a second one onto the same disk images. That is how two
# were found running on 16 September 2026, with the keystrokes going to whichever
# window `xdotool` listed last and nothing reaching the guest at all; the file's
# own record of thirty-one instances came of the same blind spot.
box_pids() {
  ps -eo pid,args \
    | grep -E 'local/bin/86Box|squashfs-root/AppRun' \
    | grep -v grep | awk '{print $1}'
}
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

# Is 86Box itself paused? Read from its toolbar button: a green play triangle
# when paused, near-white pause bars when running.
paused() {
  python3 - "$1" <<'PY'
import sys
try:
    from PIL import Image
except ImportError:
    sys.exit(1)
r, g, b = Image.open(sys.argv[1]).convert("RGB").getpixel((16, 39))
sys.exit(0 if (g > 120 and r < 80 and b < 80) else 1)
PY
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

# **How many distinct colours the guest's screen holds.**
#
# This is the number that tells one screen from another, and `screen_delta` is
# not. Measured on captures taken across an afternoon:
#
#     within one screen, two captures     0   +1485   +1172
#     between two screens                     -20175  -14115
#
# An order of magnitude apart, where the mean pixel difference gave 34 for a
# prompt appearing and 35 for a whole screen changing - indistinguishable. The
# reason is that these menus animate by *moving* things, which shifts many pixels
# and introduces almost no colour, while arriving somewhere else replaces the
# palette.
screen_colours() {
  convert "$1" -crop 667x500+0+55 -format "%k" info: 2>/dev/null || printf 0
}

# **How much the screen moved between two captures**, on a scale of 0 to 255.
#
# Printed rather than judged, because the threshold belongs to the caller: a menu
# with butterflies on it is never twice identical, so "did the screen change" and
# "did the screen *advance*" are different questions with different bounds.
#
# The comparison is on a coarse greyscale reduction of the guest's area alone.
# The emulator's own chrome - the toolbar, the status line, the clock - changes
# every second and would answer yes to everything.
screen_delta() {
  python3 - "$1" "$2" <<'DELTA'
import sys
try:
    from PIL import Image
except ImportError:
    print("0")
    sys.exit(0)

def sig(path):
    im = Image.open(path).convert("L").crop((0, 55, 667, 555))
    return list(im.resize((32, 24)).getdata())

a, b = sig(sys.argv[1]), sig(sys.argv[2])
print(int(sum(abs(x - y) for x, y in zip(a, b)) / len(a)))
DELTA
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
  resume)
    # **The emulator pauses itself, and it is our own doing.**
    #
    # 86Box's toolbar carries a play/pause button, and a click on it leaves that
    # button with the keyboard focus. Every `Return` sent afterwards - and `run`
    # sends two - presses it again. One click to resume a paused machine, and the
    # next launch pauses it back, which reads from the host exactly like a long
    # render: a black screen, the guest's monitor gone to standby, and nothing
    # written for half an hour. It cost two of those on 15 September 2026.
    #
    # So resuming is two clicks: the button, then the guest, to take the focus
    # off the button. The guest click is the same one `start` performs, and is
    # safe at the desktop - never while the game holds the Voodoo full screen.
    #
    # The state is read from the button itself: green play triangle when paused,
    # near-white pause bars when running.
    need_running; W="$(window)"
    tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
    "$0" shot "$tmp/s.png" >/dev/null 2>&1 || die "cannot read the screen"
    if paused "$tmp/s.png"; then
      say "the machine is paused - resuming"
      DISPLAY="$DISP" "$XDO" windowactivate "$W" 2>/dev/null || true
      DISPLAY="$DISP" "$XDO" mousemove 16 39 click 1 2>/dev/null || true
      sleep 1
      DISPLAY="$DISP" "$XDO" mousemove --window "$W" 300 250 click 1 2>/dev/null || true
      say "resumed, and the focus is off the button"
    else
      say "the machine is running"
    fi
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
  pad-until)
    # **A press that is confirmed rather than counted.**
    #
    # `pad-hold` sends input and returns; whether the game took it is the
    # caller's problem, and the caller has always solved it by pressing a fixed
    # number of times with fixed waits. That drifts. Each screen of this game
    # takes a different time to become responsive, so a route that worked once
    # lands a screen short the next time - which happened twice on 17 September
    # 2026 on the way to a race, and which `docs/TEST-ENVIRONMENT.md` already
    # records as the standing rule: confirm each step by a screenshot before the
    # next press.
    #
    # This is that rule, mechanised. It captures, presses, captures again, and
    # presses again only if the screen did not move. The answer is the artefact,
    # not the elapsed time.
    #
    #   pad-until 1500 a           press A until the screen advances
    #   pad-until 1200 start       the same for Start
    #
    # **What this fixes, and what it does not.**
    #
    # It removes *timing* drift: a press that landed while a menu was still
    # fading is retried instead of counted.
    #
    # **It measures the colour count, and an earlier version of this comment was
    # wrong about why.** That version said no bound on an image difference could
    # separate "the screen advanced" from "the menu animated", having tried the
    # mean pixel difference and the count of moved cells. Both do fail:
    #
    #     pair                                   mean   cells moved
    #     the same screen, two captures             0            0 %
    #     a cursor moved and an "OK?" appeared     34           92 %
    #     CAUTION -> GAME SELECT                   35           72 %
    #
    # 34 against 35 is not a discriminator. But the *number of distinct colours*
    # is, and it was not tried before the claim was written:
    #
    #     within one screen, two captures     0   +1485   +1172
    #     between two screens                     -20175  -14115
    #
    # An order of magnitude, because these menus animate by moving things -- many
    # pixels, almost no new colour -- while arriving somewhere else replaces the
    # palette. The default bound is 5000: three times the largest change measured
    # within a screen, and a third of the smallest measured between two.
    #
    # It still does not say *which* screen you are on. `game-mode` answers that
    # coarsely, and the fingerprints in `docs/TEST-ENVIRONMENT.md` more finely.
    # `DKR_DRIVE_DELTA` moves the bound and the figure is printed on every press.
    need_running; shift
    [[ $# -ge 2 ]] || die "usage: pad-until <milliseconds> <control> [control...]"
    command -v import >/dev/null || die "ImageMagick (import) is required"
    until_ms="$1"; shift
    threshold="${DKR_DRIVE_DELTA:-5000}"
    tmp_dir="$(mktemp -d)"
    trap 'rm -rf "$tmp_dir"' EXIT
    import -display "$DISP" -window root "$tmp_dir/before.png" 2>/dev/null \
      || die "cannot read the screen"
    # `before.png` is captured once and never refreshed between attempts, which
    # reads like an oversight and is not: the question is "have we left the screen
    # we started on", not "did the last press change anything". Refreshing it
    # would make a route that advances in two small steps look like two failures.
    for attempt in 1 2 3 4 5 6; do
      "$0" pad-hold "$until_ms" "$@" >/dev/null 2>&1
      # The game presents about six frames a second and a menu fades in over
      # rather more than one, so a capture taken at once reads the old screen
      # through a transition and calls it unchanged.
      sleep 3
      import -display "$DISP" -window root "$tmp_dir/after.png" 2>/dev/null || continue
      before_k="$(screen_colours "$tmp_dir/before.png")"
      after_k="$(screen_colours "$tmp_dir/after.png")"
      delta=$(( before_k > after_k ? before_k - after_k : after_k - before_k ))
      if [[ "$delta" -ge "$threshold" ]]; then
        say "pad-until: $* advanced the screen on press $attempt (delta $delta)"
        exit 0
      fi
      say "pad-until: press $attempt left the screen where it was (delta $delta)"
    done
    die "pad-until: six presses of '$*' and the screen never moved"
    ;;
  game-mode)
    # **What the game says it is doing, read from the host while it runs.**
    #
    # `pad-until` can tell that the screen moved and not what it moved to, because
    # these menus animate and no bound on an image difference separates the two.
    # The game itself has no such difficulty: it logs `gGameMode` - -1 INTRO,
    # 0 INGAME, 1 MENU, 5 LOCKUP - which is exactly the signal a route to a race
    # needs, since a race is the one that reads 0.
    #
    # It was written off as unreachable while the guest runs, on the grounds that
    # Windows 95 holds a write behind its cache. That is only half true and the
    # half that matters is the other one: `dkr_diag_commit` closes and reopens the
    # log, from the display-list report and again every three hundred presents,
    # precisely so that a program that is killed rather than closed still leaves a
    # readable tail. So the directory entry is current within a few seconds, and
    # `mtools` reads it with the dirty flag skipped - the same way every image has
    # been read off this disk mid-run today.
    #
    # **Two limits, and they are the reason this prints a line rather than a
    # verdict.** The mode is coarse: every menu screen in the game reads MENU, so
    # this cannot tell PLAYER SELECT from GAME SELECT. And it lags: the line is
    # printed with the display-list report, once in sixty lists, which at this
    # target's frame rate is on the order of ten seconds.
    need_running
    command -v mcopy >/dev/null || die "mtools is required"
    mode_img="$VM/transfer.img@@$((63 * 512))"
    mode_log="$(mktemp)"
    trap 'rm -f "$mode_log"' EXIT
    # **Three tries, because the read races the writer.** The guest reopens this
    # log every few seconds and a copy taken across that moment comes back short
    # or empty - observed on 17 September 2026, on round twelve of a route, where
    # a single attempt returned nothing and the caller had no way to tell "the
    # game stopped" from "the file was busy". A transient empty answer that looks
    # like a verdict is the failure this whole session has been paying for.
    mode_line=""
    for mode_try in 1 2 3; do
      if MTOOLS_SKIP_CHECK=1 mcopy -o -i "$mode_img" \
           ::/dkr-runtime-data/logs/runtime.log "$mode_log" 2>/dev/null; then
        # `|| true` is load-bearing. Under `set -o pipefail` a `grep` that matches
        # nothing returns 1, the pipeline returns 1, the assignment inherits it
        # and `set -e` ends the script - exit 1, not a word printed. Which is the
        # *normal* case here: the log exists from the first second and the first
        # mode line arrives sixty display lists later, so every call during boot
        # killed the script instead of retrying. The previous fix corrected the
        # same mistake one line below and left this one, because it was found by
        # reading rather than by running.
        mode_line="$(grep '\[game\] gGameMode=' "$mode_log" | tail -1 || true)"
        # `[[ ... ]] && break` would be wrong here and was: when the test fails it
        # returns 1, that becomes the `if` block's status, and `set -e` ends the
        # script with no message at all. Caught by running the command against a
        # guest that had not written its log yet - exit 1, nothing printed, which
        # is the silent failure this file spends its comments warning about.
        if [[ -n "$mode_line" ]]; then break; fi
      fi
      sleep 2
    done
    if [[ -z "$mode_line" ]]; then
      die "no game mode line after three reads - the guest may not be running it"
    fi
    say "$mode_line"
    ;;
  pad-until-screen)
    # **Press until the screen is the one named, by fingerprint.**
    #
    #   pad-until-screen start 90621        walk to PLAYER SELECT
    #   pad-until-screen start 90621 4000   ... with a wider window
    #
    # The colour counts in `docs/TEST-ENVIRONMENT.md` identify a screen across a
    # reboot, so a route can walk to one instead of pressing a fixed number of
    # times. What it must not do is test a *floor*.
    #
    # An inline version of this used `-gt 85000` to find PLAYER SELECT, which
    # holds about 90,500 - and stopped on a screen holding 127,788 on
    # 17 September 2026, because every richer screen passes a floor and this game
    # has them up to 150,590. The route that followed ran six presses against the
    # wrong screens and its measurement had to be thrown away. A window is the
    # whole fix: the target, plus or minus a tolerance that defaults to 3000 -
    # twice the largest drift measured within one screen, and well inside the
    # smallest gap measured between two.
    need_running; shift
    [[ $# -ge 2 ]] || die "usage: pad-until-screen <control> <colours> [tolerance]"
    ps_control="$1"; ps_target="$2"; ps_tol="${3:-3000}"
    command -v import >/dev/null || die "ImageMagick (import) is required"
    ps_shot="$(mktemp --suffix=.png)"
    trap 'rm -f "$ps_shot"' EXIT
    for ps_try in 1 2 3 4 5 6 7 8 9 10; do
      import -display "$DISP" -window root "$ps_shot" 2>/dev/null \
        || die "cannot read the screen"
      ps_k="$(screen_colours "$ps_shot")"
      ps_gap=$(( ps_k > ps_target ? ps_k - ps_target : ps_target - ps_k ))
      if [[ "$ps_gap" -le "$ps_tol" ]]; then
        say "pad-until-screen: arrived after $((ps_try - 1)) press(es) ($ps_k colours, $ps_gap off)"
        exit 0
      fi
      say "pad-until-screen: $ps_k colours, $ps_gap off - pressing $ps_control"
      "$0" pad-hold 1400 "$ps_control" >/dev/null 2>&1
      sleep 5
    done
    die "pad-until-screen: ten presses and never within $ps_tol of $ps_target"
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
