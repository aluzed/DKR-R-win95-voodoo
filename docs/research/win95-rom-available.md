# The ROM was there all along

Observed on 15 August 2026, after a dozen assertions to the contrary.

## What happened

The project expects the ROM at `extern/dkr-decomp/build/dkr.us.v77.z64`. That path
does not exist — `extern/` contains nothing but `n64-modern-runtime`. I concluded
from that that the ROM was absent, and built several tickets around that
constraint.

It was at `/var/www/Diddy-Kong-Racing/build/dkr.us.v77.z64`, in the neighbouring
repository whose `gbi.h`, `f3ddkr.h` and `textures_sprites.c` I had been reading
for hours for E05-S03, E05-S06 and E05-S08.

**A missing path is not a missing file.** I never searched by name, and I repeated
the conclusion in a dozen commits and documents without rechecking it once. A
starting assumption, copied forward, becomes a constraint in fact.

## What it cost

Six tickets carried criteria marked "blocked by the ROM" which were not. Several
design justifications invoked the absence of the ROM when they should have invoked
something else — E09-S02's synthetic scene, for instance, keeps all its value
because one knows its answer in advance, and not because there was no ROM.

Nothing is to be thrown away: the measurements made stay valid, and the witnesses
written stay useful. What is to be corrected are the reasons given.

## The real state, measured

The ROM is valid — header `80371240`, title `Diddy Kong Racing`, code `NDYE`,
12 MiB, digest `4f0e07f0eeac7e5d7ce3a75461888d03`.

And the game **starts on Windows 95**:

    [boot][input] keyboard: WASD=stick arrows=d-pad Space=A ...
    [boot][rom] validated and registered
    [boot] runtime initialized; waiting for first safe VI state
    [boot][audio] frequency=48000 (diagnostic backend)
    [boot] VI initialized; starting recompiled DKR entrypoint
    [boot][vi] present=60 ... present=2760

Two thousand seven hundred and sixty frames presented, that is forty-six seconds
at 60 Hz. A protection fault appears along the way — the number of frames before
it appears varies from one run to the next — but **the video thread survives it**
and carries on presenting.

The `[boot][crash]` handler did not fire: it does not cover the faulting thread.
That is the next point to deal with.

## Three tooling obstacles cleared along the way

**`stderr` was not recoverable.** The whole runtime log goes through it, including
the crash handler that prints exception code, address and RVA. But Windows 95's
COMMAND.COM has no `2>&1` syntax. The runtime now redirects `stderr` to a file on
this target, unbuffered — a crash leaves no time to flush a buffer.

A first version of that redirection was placed in the `#else` branch of
`#ifdef _WIN32`, whereas the entry point under Windows is `WinMain`. It was never
compiled, and the symptom was mute: the program ran, the file did not appear,
nothing said why. The call is now in `DkrMain`, independently of the entry point.

**ScanDisk was blocking every boot.** A crash leaves the volume dirty, and Windows
runs ScanDisk at the next boot, where it swallows the keystrokes meant for the
desktop. The symptom is disconcerting: the program "does not start" when it was
never launched at all. `AutoScan=0` in `MSDOS.SYS` removes the cause. The disk
image is backed up as `win95.img.before-autoscan`.

**The mouse is captured by the emulator**, so a click in absolute coordinates
means nothing to the guest. Only the keyboard is reliable for driving the machine.
