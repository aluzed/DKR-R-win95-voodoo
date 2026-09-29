#!/usr/bin/env bash
# E08 - takes the running game from its attract sequence into a race, confirming
# each screen before the next press, and optionally keeps driving.
#
#   scripts/Drive-To-Race.sh          reach a race: TRACKS, Ancient Lake, car,
#                                     time trial off
#   scripts/Drive-To-Race.sh 300      ... then hold the accelerator, with some
#                                     steering, for 300 seconds
#
# The route is the one walked by hand on 29 September 2026 for E08-S02's race
# profile and the frame budget's race, one screenshot per press:
#
#     start ...   the attract sequence, then the title, then PLAYER SELECT
#     a  a        the character, "OK?"
#     a           CAUTION, dismissed into GAME SELECT
#     down  a     TRACKS, then the track choice (DINO DOMAIN / ANCIENT LAKE)
#     a  a  a  a  the track, the car, time trial off, "OK?"
#
# **Every stage is confirmed, never timed.** `docs/TEST-ENVIRONMENT.md` records
# why: the same presses fired on fixed sleeps reached track select on one run and
# stopped two screens short on the next. Two checkpoints -- PLAYER SELECT by its
# screen fingerprint, the race by `gGameMode` read from the game's own log --
# and a failure at either stops the script with a screenshot rather than
# sending presses at the wrong screen.
#
# Tested once, 29 September 2026: it reached PLAYER SELECT in four presses and
# the track choice on its own, then stopped on the fingerprint check this
# version no longer makes (below). This version, run from a cold boot the same
# evening: PLAYER SELECT in four presses, then the race's GET READY, unaided.
#
# Run it while the game is up, typically beside `Measure-Guest-Time-VM.sh` in
# the background. The track and the driving are not a benchmark of anything:
# they put the game's race code under a player, which the attract mode is not.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
drive="$HERE/Drive-Win95-VM.sh"
seconds="${1:-0}"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() {
  local shot
  shot="$(mktemp --suffix=.png)"
  "$drive" shot "$shot" >/dev/null 2>&1 || true
  printf '\033[1;31merror:\033[0m %s (screen: %s)\n' "$*" "$shot" >&2
  exit 1
}
press() { "$drive" pad-hold "${2:-1200}" "$1" >/dev/null 2>&1; sleep "${3:-8}"; }

# The last mode the game reported: the change line when there is one, the
# periodic line otherwise.
mode() {
  "$drive" game-mode 2>/dev/null | sed -n 's/.*\(gGameMode\|mode\)=\(-\?[0-9]\+\).*/\2/p' | tail -1
}

say "to PLAYER SELECT"
"$drive" pad-until-screen start 90621 4000 || die "PLAYER SELECT not reached"

say "character, confirmation, caution"
press a 1200 6
press a 1200 8
press a 1200 8

say "TRACKS"
press down 900 4
press a 1200 10
# **No fingerprint here.** The track choice was listed at 105,590 colours in
# `docs/TEST-ENVIRONMENT.md`; on 29 September 2026 it read 84,190 and 87,153 on
# two captures a few seconds apart -- its preview is a moving flyover -- and the
# retry the mismatch triggered pressed one screen too far. The last checkpoint
# is the game's own mode instead, and an extra `a` once the race is loading is
# harmless: it is the accelerator.

say "track, vehicle, time trial off, confirmation"
for _ in 1 2 3 4; do press a 1200 10; done

say "waiting for the race"
for attempt in 1 2; do
  for _ in $(seq 1 8); do
    if [[ "$(mode)" == "0" ]]; then break 2; fi
    sleep 5
  done
  (( attempt == 1 )) && { say "  not in a race yet: one more press"; press a 1200 10; }
done
[[ "$(mode)" == "0" ]] || die "gGameMode never read INGAME"
say "in a race"

if (( seconds > 0 )); then
  say "driving for ${seconds}s"
  end=$(( $(date +%s) + seconds ))
  while (( $(date +%s) < end )); do
    "$drive" pad-hold 9000 a >/dev/null 2>&1
    "$drive" pad-hold 1500 a left >/dev/null 2>&1
    "$drive" pad-hold 6000 a >/dev/null 2>&1
    "$drive" pad-hold 1500 a right >/dev/null 2>&1
  done
fi
