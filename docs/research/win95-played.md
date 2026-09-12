# The port plays

Driven on 12 September 2026 from the host, with no screen and no hands:
`scripts/Drive-Win95-VM.sh key`, into a build that renders on the Voodoo.

## What happened

| | |
|---|---|
| the title screen | the logo, the kart, `START` and `OPTIONS` |
| `Return` ×n, `space` ×n | **GAME SELECT** — `ADVENTURE` and `TRACKS`, on their wooden plaques |
| `Down`, `space` | the file screen — three `NEW` slots, `ENTER YOUR INITIALS`, the letter carousel, `AA` already typed by the presses that got there |
| `Left`, `space` | Timber's Island loading in: sky, foliage, a dirt path |

Every screen is the card's own output. The keyboard is E06-S02's, message-based
with the latch that E06-S01 measured as necessary at 170 ms a frame — four
keystrokes sent and one seen, before it existed.

This is the first time the port has been **played** rather than watched. Until
now every image in this repository came from the game driving itself: the intro,
the attract sequence, a demo race. These came from keys.

## What it establishes, and what it does not

It establishes that the chain holds through menu navigation — text, plaques, a
carousel, a 3D hub loading — and that the input path reaches the game's own menu
logic rather than merely arriving in a buffer.

It does **not** establish a lap of a level, which is what E09-S02's corpus still
lacks. And the reason is worth recording precisely, because it cost a run:

> **`gGameMode` stays at 1 (MENU) through Timber's Island.** The hub's fly-in, the
> foliage, the path — all of it is drawn under MENU, with only `level` changing
> from `0x801FB780`. A capture anchored on `DKR_CAPTURE_MODE=0` (INGAME) therefore
> never fires there, and mode 0 means a race proper.

So the corpus's first gameplay scene needs the navigation carried further. And
carrying it further ran into something worth its own section.

## The directions were not intermittent; one of them was mistranslated

Navigating properly means *choosing* menu items rather than accepting the first
one, and that needs a direction. Measured on the letter carousel, where a move is
unmistakable — the strip reads `? SP DEL Ok A B C D` and the selection is drawn
with a bright outline:

| sent | effect |
|---|---|
| `space` (A), `Return` (Start) | every time: letters typed, screens advanced, five screens deep |
| `d` ×4 — the stick right | the selection moved **A → E**, four for four |
| `a` ×4 — the stick left | **nothing**, twice |
| `Right` ×2 — the D-pad | nothing |
| `q` ×4 | the selection moved **E → A**, four for four |

**The guest's layout is AZERTY and the host sends scancodes.** `a` arrives at the
guest as `Q`, which `runtime_platform.cpp` binds to the L button, so the stick
never went left. `d` is the same key on both layouts and worked perfectly. `q`
arrives as `A` and is the stick-left the port is waiting for.

`Drive-Win95-VM.sh` already says this — about its `type` subcommand, which routes
through `tools/win95/azerty_keys.py` because "no Windows path is written without a
`:` or a `\`". It did not say it about `key`, and `key` is what one drives a game
with.

**Nothing was intermittent.** The first write-up of this called it that, built a
`hold` subcommand for a latch race, and reasoned about polls consuming each other
— all of it addressed to a symptom produced by two keys, one of which was being
mistranslated. Three right and four left is perfectly consistent behaviour once
you know that only one of the two arrives.

### What replaces it

`Drive-Win95-VM.sh pad <left|right|up|down|a|b|z|start|l|r>` names the game's
controls by what they **do** and sends whatever physical key the guest's layout
needs. `left` is `q`, `up` is `z`, `right` and `down` are themselves. The trap is
closed at its source rather than left for each caller to remember.

Verified by round trip: `pad right ×4` walks the carousel to `B`, `pad left ×4`
walks it back past `A` to **`Ok`** — the item that could not be reached at all
before, and the reason every earlier run ended up on `ADVENTURE` and the hub.

`hold` is kept. A held key is a real input a tap is not — accelerating out of a
corner needs one — and its own comment now says that the asymmetry it was built to
explain had another cause.
