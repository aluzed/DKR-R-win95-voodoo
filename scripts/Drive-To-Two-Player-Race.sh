#!/usr/bin/env bash
# E06-S02 - takes the running game into a two-player race: player one on the
# keyboard's left side, player two on the numeric keypad.
#
#   scripts/Drive-To-Two-Player-Race.sh          Ancient Lake, cars, 6 racers
#   scripts/Drive-To-Two-Player-Race.sh 300      ... then both accelerate for
#                                                300 seconds
#
# The route, walked by hand on 2 October 2026, one screenshot per press:
#
#     start ...     the attract sequence, the title, PLAYER SELECT
#     KP_0          player two joins: a "2" marker appears
#     a  KP_0       each picks a character, "OK?"
#     a             two players go straight to the track choice
#     a             Ancient Lake
#     a  KP_0       each picks the car
#     right right a NUMBER OF RACERS: 6
#     a             "OK?", then the race
#
# The keypad is read by scan code (`dkr_window_keypad_down`), so NumLock does
# not matter -- the test machine boots with it off and xdotool cannot turn it
# on. The race is confirmed by `gGameMode`, as in `Drive-To-Race.sh`.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
drive="$HERE/Drive-Win95-VM.sh"
seconds="${1:-0}"
say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
p1() { "$drive" pad-hold "${2:-600}" "$1" >/dev/null 2>&1; sleep "${3:-4}"; }
p2() { "$drive" hold "$1" "${2:-600}" >/dev/null 2>&1; sleep "${3:-4}"; }
mode() {
  "$drive" game-mode 2>/dev/null | sed -n 's/.*\(gGameMode\|mode\)=\(-\?[0-9]\+\).*/\2/p' | tail -1
}

say "to PLAYER SELECT"
"$drive" pad-until-screen start 90621:486 4000 || die "PLAYER SELECT not reached"
say "player two joins, both pick a character"
p2 KP_0
p1 a 600 2; p2 KP_0
p1 a 600 8
say "Ancient Lake, two cars, six racers"
p1 a 600 8
p1 a 600 2; p2 KP_0 600 5
p1 right 400 2; p1 right 400 2; p1 a 600 6
p1 a 600 6
for _ in $(seq 1 12); do [[ "$(mode)" == "0" ]] && break; sleep 5; done
[[ "$(mode)" == "0" ]] || die "gGameMode never read INGAME"
say "in a two-player race"

if (( seconds > 0 )); then
  say "both accelerating for ${seconds}s"
  end=$(( $(date +%s) + seconds ))
  while (( $(date +%s) < end )); do
    "$drive" pad-hold 6000 a >/dev/null 2>&1 &
    "$drive" hold KP_0 6000 >/dev/null 2>&1
    wait
  done
fi
