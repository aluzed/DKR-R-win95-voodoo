#!/usr/bin/env bash
# Run a build of the game on the test machine and report what share of the time
# it spent executing guest code.
#
#   scripts/Measure-Guest-Time-VM.sh build/win95/bin/DKRR.EXE           glide
#   scripts/Measure-Guest-Time-VM.sh build/win95-narrow/bin/DKRR.EXE    null 150
#
# The instrument is patch 0041's `DKR_TRACE_CPU`, which stamps every context
# switch and reports every five seconds: guest-run against wall, both read from
# the guest's own 8254, so the ratio is in emulated time and does not move with
# whatever the host is doing.
#
# **Compare at the same wall offset, never at the end.** The ratio falls through
# a run -- 88% during the intro's loading, 71% a minute later -- so two runs of
# different lengths are not comparable at their last sample. The report prints
# the whole series for that reason, and the comparison that matters is
# sample-by-sample.
#
# `null` selects the diagnostic renderer, which counts display lists instead of
# drawing them. Guest-run then holds the game's own work with the graphics
# pipeline taken out of it, which is what separates "the recompiled code is the
# cost" from "the renderer is the cost".

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${DKR_WIN95_PREFIX:-$HOME/.local/dkr-win95}"
vm="${DKR_WIN95_VM:-dkr-p2-voodoo2}"
image="$prefix/vm/$vm/transfer.img@@32256"
drive="$root/scripts/Drive-Win95-VM.sh"
push="$root/scripts/Push-To-Win95-VM.sh"

executable="${1:?usage: Measure-Guest-Time-VM.sh <DKRR.EXE> [glide|null] [seconds]}"
renderer="${2:-glide}"
seconds="${3:-150}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

# The launcher, as a batch file: Windows 95's COMMAND.COM has no way to set an
# environment variable for one command, and the Run dialog has no environment
# of its own.
{
    printf '@ECHO OFF\r\n'
    printf 'SET DKR_TRACE_CPU=1\r\n'
    [[ "$renderer" == "null" ]] && printf 'SET DKR_RENDERER=null\r\n'
    printf 'D:\\DKRR.EXE D:\\DKR.Z64\r\n'
} > "$work/MEASURE.BAT"

cp "$executable" "$work/DKRR.EXE"

say "stopping the machine so the guest sees the new files"
"$drive" stop >/dev/null 2>&1 || true
"$push" --clear-dirty >/dev/null 2>&1 || true

# A stale log read as this run's output is the failure this harness is most
# prone to, so the old one goes before the new one can be confused with it.
"$prefix/bin/mdel" -i "$image" "::/dkr-runtime-data/logs/runtime.log" >/dev/null 2>&1 || true
"$prefix/bin/mdel" -i "$image" "::/DKRR.LOG" >/dev/null 2>&1 || true

say "pushing $(basename "$executable") ($(stat -c%s "$executable") bytes)"
"$push" "$work/DKRR.EXE" "$work/MEASURE.BAT" >/dev/null

say "booting"
"$drive" boot >/dev/null

say "launching with the $renderer renderer"
if [[ "$renderer" == "null" ]]; then
    # The diagnostic renderer never takes the screen, so `run-glide`'s "did it
    # go black" test cannot see it start, and the blind `run` fails often enough
    # -- a Start menu that opens the task list instead, a window that has not
    # taken the focus yet -- that an unchecked launch means measuring nothing
    # for two and a half minutes and finding out afterwards.
    #
    # The taskbar is the check. A program that started has a button there, and
    # the strip is otherwise identical from one screenshot to the next.
    taskbar_ink() {
        "$drive" shot "$work/screen.png" >/dev/null 2>&1 || return 1
        python3 -c "
from PIL import Image
import sys
strip = Image.open(sys.argv[1]).crop((0, 530, 400, 556)).convert('L')
print(sum(1 for pixel in strip.getdata() if pixel < 100))
" "$work/screen.png"
    }

    for _ in 1 2 3; do "$drive" key Escape >/dev/null 2>&1; done
    sleep 2
    before="$(taskbar_ink || echo 0)"
    launched=0
    for attempt in 1 2 3; do
        "$drive" run "D:\\MEASURE.BAT" >/dev/null 2>&1 || true
        sleep 15
        after="$(taskbar_ink || echo 0)"
        if (( after > before + 100 )); then
            say "  started on attempt ${attempt} (taskbar ${before} -> ${after})"
            launched=1
            break
        fi
        say "  attempt ${attempt} did not start it (taskbar ${before} -> ${after})"
        for _ in 1 2 3; do "$drive" key Escape >/dev/null 2>&1; done
    done
    if (( launched == 0 )); then
        echo "the game never started: measuring nothing, stopping here" >&2
        "$drive" stop >/dev/null 2>&1 || true
        exit 1
    fi
else
    "$drive" run-glide "D:\\MEASURE.BAT" 300 >/dev/null
fi

say "running for ${seconds}s"
for _ in $(seq 1 $(( (seconds + 9) / 10 ))); do sleep 10; done

say "quitting"
"$drive" key alt+F4 >/dev/null 2>&1 || true
sleep 10
"$drive" stop >/dev/null 2>&1 || true
"$push" --clear-dirty >/dev/null 2>&1 || true

"$prefix/bin/mcopy" -i "$image" "::/dkr-runtime-data/logs/runtime.log" "$work/runtime.log" \
    >/dev/null 2>&1 || { echo "no runtime log: the run produced nothing" >&2; exit 1; }

python3 - "$work/runtime.log" "$renderer" <<'PYTHON'
import re
import sys

pattern = re.compile(r"guest-run=(\d+) us wall=(\d+) us switches=(\d+)")
rows = []
for line in open(sys.argv[1], errors="replace"):
    found = pattern.search(line)
    if found:
        rows.append(tuple(int(g) for g in found.groups()))

if len(rows) < 3:
    print("only %d trace samples: the instrument said nothing" % len(rows))
    raise SystemExit(1)

print()
print("  renderer: %s" % sys.argv[2])
print("  %8s %12s %12s %10s %8s" % ("wall s", "guest us", "switches", "busy", "sw/s"))
previous = rows[0]
for guest, wall, switches in rows[1:]:
    span = wall - previous[1]
    if span == 0:
        continue
    print("  %8.1f %12d %12d %9.1f%% %8.2f"
          % (wall / 1e6, guest, switches,
             100.0 * (guest - previous[0]) / span,
             (switches - previous[2]) / (span / 1e6)))
    previous = (guest, wall, switches)

last = rows[-1]
print()
print("  cumulative at %.1f s: guest %.1f s, busy %.1f%%, %.2f switches/s"
      % (last[1] / 1e6, last[0] / 1e6, 100.0 * last[0] / last[1],
         last[2] / (last[1] / 1e6)))
PYTHON
