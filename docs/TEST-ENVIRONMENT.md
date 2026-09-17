# The emulated test environment

The recipe for the Windows 95 / 3dfx machine on which the port is developed.
Deliverable of
[E09-S01](stories/E09-qa/E09-S01-emulated-test-environment.md).

## Why this environment comes before the rest

The port's "edit, run, observe" cycle goes through a Windows 95 machine with a
3dfx card. On real hardware, each iteration costs a file copy, a reboot, and —
when Glide crashes full-screen — a reinstallation. The emulator brings that cycle
down to a few seconds, and that is what makes the inevitable trial and error of
[E05-S01](stories/E05-glide/E05-S01-glide-init-and-buffers.md) practicable.

It does not replace validation on real hardware
([E09-S04](stories/E09-qa/E09-S04-real-hardware-validation.md)): it makes it rare.

## Setting up

```bash
scripts/Setup-Win95-TestVM.sh
```

The script installs nothing system-wide — everything goes into
`~/.local/dkr-win95` — and needs no root privileges. It fetches 86Box and the BIOS
set, installs `mtools` locally, creates the disk images and writes the machine
configuration.

| Item | Location |
|---|---|
| 86Box v6.0 | `~/.local/dkr-win95/opt/86box/squashfs-root/AppRun` |
| BIOS set | `~/.local/dkr-win95/opt/86box/roms` |
| Machine | `~/.local/dkr-win95/vm/dkr-p2-voodoo2` |

The AppImage is **extracted** rather than mounted: FUSE is often absent from build
machines, and extraction removes that dependency.

## Preparing the installation

**You must supply your own Windows 95 OSR2.5 medium** — proprietary Microsoft
software, which the scripts do not download and cannot download.

```bash
scripts/prepare_win95_install.py --iso /path/to/W95.iso
```

This script creates the disk images, **partitions** and formats them, then copies
onto them the installation source extracted from the ISO:

| Disk | Image | Contents |
|---|---|---|
| A: | `freedos-boot.img` | FreeDOS 1.4 boot |
| C: | `win95.img` — 1,023 MiB | system, to be partitioned then installed |
| D: | `transfer.img` — 504 MiB | host ↔ guest transfer |
| E: | `install.img` — 128 MiB | Windows 95 source, 46 MiB |

Two points are worth knowing, because they are the usual cause of failure at this
step:

- **A DOS hard disk requires a partition table.** A FAT volume written directly at
  the start of the image, without an MBR, is not seen by DOS. The script therefore
  writes an MBR and places the file system from sector 63 onwards.
- **The source is on a hard disk, not on the CD.** That removes any dependence on
  a CD-ROM driver under DOS, which is the most fragile point of an emulated
  Windows 95 installation.

The French OSR2.5 CD contains no `SETUP.EXE`: its installation program is called
**`INSTALL.EXE`** (a Windows 3.x NE executable, which loads `DOSSETUP.BIN` then
`WINSETUP.BIN`). The ISO is not bootable — it has no El Torito descriptor — hence
the FreeDOS floppy.

## Installation

C: is already partitioned, formatted and marked active: **neither `FDISK` nor
`FORMAT` is needed**, and the reboot `FDISK` imposes between the two is avoided.
The FreeDOS floppy runs `E:\WIN95\INSTALL.EXE` directly.

```bash
scripts/Run-Win95-VM.sh          # on a machine with a screen
```

On the very first boot, the BIOS displays:

```text
CMOS checksum error - Defaults loaded
Press F1 to continue, DEL to enter SETUP
```

That is normal — the CMOS is blank. Press **F1**. The message does not reappear
once the machine has been shut down cleanly.

Then install the 3dfx drivers for the Voodoo 2 in the guest.

## Driving without a screen

The development machine does not necessarily have a graphical session.
`Drive-Win95-VM.sh` runs 86Box on a virtual X display, captures the screen and
injects keys — which makes the environment drivable from a terminal, and provides
the foundation of
[E09-S02](stories/E09-qa/E09-S02-visual-comparison-harness.md)'s visual comparison
harness.

```bash
scripts/Drive-Win95-VM.sh start                  # Xvfb + 86Box + keyboard capture
scripts/Drive-Win95-VM.sh key F1                 # get past the CMOS warning
scripts/Drive-Win95-VM.sh key Return             # FreeDOS language menu
scripts/Drive-Win95-VM.sh shot screen.png        # see where the machine has got to
scripts/Drive-Win95-VM.sh type "DIR E:\WIN95"
scripts/Drive-Win95-VM.sh stop
```

Three traps each cost a good hour if one does not know them:

- **`xdotool key --window` does not work.** It goes through `XSendEvent`, which Qt
  ignores. One must set the X focus and then use XTEST, that is `xdotool key`
  *without* `--window`.
- **86Box only routes the keyboard to the emulated machine after a click** in its
  window, which captures the devices. Without that click, the keys go to the
  emulator's interface. `Drive-Win95-VM.sh start` performs that click; `grab` does
  it again if the capture has been released.
- **The guest has its own keyboard layout, and it applies to the scancodes.**
  `xdotool type "24796"` on a French Windows produces `é'èç-`: the host sends the
  top-row keys, and the guest reads them as AZERTY. Two practical consequences:

  | What one wants | What must be sent |
  |---|---|
  | a digit | `shift+<digit>` — in AZERTY the top row is shifted |
  | `A Q Z W M` | the crossed keys; to be avoided in test strings |
  | the numeric keypad | unusable, NumLock is off in the guest |

  The safest course, for free text, is to use only letters identical in both
  layouts.

Diagnosis by screenshot is what allowed the wait on `Press F1` to be found: from
the outside, a machine stuck at the BIOS and a machine that does not boot are
indistinguishable.

### `run-glide`, and why `run` alone is not enough

`run` walks the Start menu blind — `Ctrl+Esc`, three `Up`s, `Return`. That works
until `Ctrl+Esc` raises the **task list** instead of the Start menu, which this
guest does intermittently: the three `Up`s then walk the task list and `Return`
opens whatever sits under them, leaving a window open and the program unstarted.

Nothing announces it. The measurement that follows reads the *previous* run's
output file and produces a number that looks perfectly reasonable. On 14
September 2026 that happened three times in one session, and once the stale
number was briefly attributed to a code change that had in fact been reverted.

```bash
scripts/Drive-Win95-VM.sh run-glide "D:\REPLAY.EXE --both D:\CAP0800.BIN" 800
```

A Glide program takes the whole screen, so "did it start" is one pixel.
`run-glide` waits for the screen to go black, retries the launch up to four times
if it does not, and returns only when the desktop is back. Use it for **anything
whose output file is read back afterwards**; `run` remains right for the game,
which is not expected to exit.

**The recovery between attempts sends `Escape` and nothing else, and that is a
rule.** The first version pressed `Return` "in case it was swallowed" and
`Alt+F4` to clear a stray window. On the desktop `Alt+F4` raises the shutdown
box, `Return` takes its default — and on 15 September 2026 the pair switched the
machine off in the middle of a corpus sweep. Escape closes a menu, dismisses a
dialog, and does nothing anywhere else: a recovery should not be able to do more
than that.

**For a sweep, do not launch eight times.** The Start menu is the unreliable part
— under load the `Up` keys are dropped and the menu stays open with nothing
selected — so the launch is what fails, not the program. A `.BAT` on `D:` that
runs each capture and copies `RPLCARD.BMP` / `RPLSOFT.BMP` aside under a name of
its own turns eight fragile launches into one, and the host waits for the last
file the batch writes.

**And such a batch needs `START /W`.** A `.BAT` that names a Win32 program does
not wait for it: `COMMAND.COM` hands it to the shell and runs the next line at
once. Written the obvious way, an eight-scene sweep launches eight replays on top
of each other, each copy takes whatever image happens to be on disk, and the
first symptom is a directory with no copies in it at all.

```dos
START /W D:\REPLAY.EXE --both D:\CAP0050.BIN
COPY D:\RPLCARD.BMP D:\KP0050.BMP
```

**`run-glide` is for a program that takes the screen at once**, and *no* form of
`REPLAY.EXE` is one. `--both` spends minutes in the software oracle with the
desktop still showing and only then opens the card; `--card` was tried on
16 September 2026 on the assumption that it would take the screen straight away,
and the watcher reported "the program never took the screen" for a run that had
already written both its log and its image. A screen watcher calls those a failed
launch and retries on top of a program that is working perfectly - four times, so
the scene is replayed four times over. For any of them, watch the **artefact**:
note the output file's directory entry, launch with `run`, and poll until the
entry changes.

```bash
stamp() { mdir -i "$IMG" :: | grep '^RPLCARD'; }
before="$(stamp)"; scripts/Drive-Win95-VM.sh run "D:\REPLAY.EXE --both D:\CAP0050.BIN"
until [ "$(stamp)" != "$before" ]; do sleep 15; done
```

**Use `pad-hold`, not `pad`, against the game.** `pad` presses and releases in a
few milliseconds; this target presents about **twelve frames a second**, and the
game samples input once a frame, so a keystroke falls between two polls and is
never seen. Measured on 17 September 2026: three `pad start` presses produced
nothing at all, and `pad-hold 1200 start` landed three times out of three -

    [input] win95 buttons=0x1000     one line per press

taking the game out of its attract sequence and into PLAYER SELECT. The same
brevity is harmless on the desktop, where Windows queues the keystroke; the game
polls a level rather than a queue, so only the hold works.

This cost three VM runs and very nearly a wrong conclusion: with `pad`, the port
looks as though it has no input at all.

**The menu path, mapped one press at a time (17 September 2026).** From a cold
start, with `pad-hold 1200` for every press - a tap is too brief, see below:

    4 x start   the attract sequence yields the title screen (START / OPTIONS)
    start       PLAYER SELECT
    a           GAME SELECT
    a           CAUTION (the controller-pak notice), which waits for a press
    a           dismisses it, into the mode choice
    down        moves the cursor - `a` alone confirms and never moves it
    a           TRACK SELECT: DINO DOMAIN / ANCIENT LAKE, with its preview

Seven presses from a cold start reach track select, where seven sent into the
attract loop got nowhere at all. `CAP0420` was captured at GAME SELECT and
`CAP0700` at the caution screen; track select is one press from a race.

Every press is `pad-hold 1200` (or 900 for a direction). A tap is too brief - see
below.

**Send that sequence exactly** - and even then, do not fire it blind. Changing the
press counts desynchronises the route: five `start` then three `a`, instead of four
`start` then `start a a a down a`, ends on GAME SELECT rather than at track select.

But the exact sequence fired blind is **not reliable either**. The same seven
presses that reached track select when each was screenshotted landed on the caution
screen on the next run, with the same waits. Each screen takes a variable time to
become responsive, so fixed sleeps drift.

**The route is reliable only when each step is confirmed - and a screenshot is not
a confirmation.** Adding a shot between presses only *records* where the run went;
the sequence still advances on a fixed sleep and still drifts. Measured twice on
17 September: the same seven presses with a screenshot after each reached track
select once and ended on GAME SELECT the next time, two screens short.

Confirming means **reading** the screen and only then sending the next press, which
a single shell command cannot do. It needs one press per step with the screen
examined in between. That is slower in turns and it is the only thing that has
worked reliably.

Five ten-minute runs on 17 September captured a screen the corpus already had,
every one of them because a batch was fired and inspected only at the end.

**A capture fires wherever the game is, so check the screen before trusting it.**
`CAP1100` was armed for a vehicle-select screen and came back with GAME SELECT's
fill. The screenshot taken right after the presses shows GAME SELECT too, so the
capture is *consistent* - the route simply never got there, for the reason above.

There is **no evidence here that the menus idle back**: this note claimed that for
one commit and the claim was withdrawn, because the screenshot and the capture show
the same screen. Whether a menu left alone returns anywhere is unmeasured. Take a
screenshot near the capture's moment when it matters, rather than assuming either
way.

**Drive from the title screen, not from the attract loop.** The game opens on an
attract sequence - a character carousel, then the copyright logo, then a flyover -
and presses sent into it do not navigate: mapping them one at a time on
17 September 2026, four holds landed back on the **title screen** (START /
OPTIONS) rather than deeper in. Sequences built from the attract loop go round and
return, which is why batches of `a` presses plateau at GAME SELECT.

The title screen is the real entry point. Screenshot after **every** press when
building a sequence; sending a batch and inspecting only the end cannot tell
"went deeper" from "went round".

**"No directory slots" means the root directory is full, not that anything is
wrong.** D: is FAT16 and its *root* holds a fixed number of entries - a few hundred
- regardless of how many megabytes are free. A session that captures a dozen eight
-megabyte scenes, a log beside each and an image or two per run reaches it, and the
push then fails with that message while `mdir` still reports hundreds of megabytes
free.

Met on 17 September 2026 with 249 MB free and no slots left. The fix is to delete
what has already been pulled to the host:

    mdel -i "$IMG" ::CAP0220.BIN

Worth knowing because the message names neither the cause nor the remedy, and the
obvious reading - a full or corrupt disk - is wrong on both counts.

**Never start a machine run while another is still in flight**, and check the
*previous* task rather than the new one. On 17 September 2026 a screenshot-mapping
run produced no screenshots at all and exited cleanly: the run before it was still
finishing, so two sequences drove one machine and the second one's `start` found an
instance already up. The output said nothing because every step had been redirected
away.

Two symptoms worth recognising, because they look like a broken script rather than
a collision: **a run that exits 0 having produced none of its artefacts**, and a
guest that is on a screen no press in the current sequence could have reached.

The waiting loop has to watch the task that is running, not the one just launched.

**Do not touch the emulator's window while a sweep runs.** `Alt+F4` and the other
`Alt` combinations reach *86Box's own menu bar*, not the guest: one of them
paused the machine mid-sweep on 15 September 2026, and from the host the pause
looks exactly like a long render — a black screen, the guest's monitor gone to
standby behind it, and nothing written for a quarter of an hour. Poll the disk
instead; the screen says less than the directory does.

Once Windows and the 3dfx drivers are installed, freeze the state:

```bash
scripts/Run-Win95-VM.sh --snapshot
```

and come back to it after every attempt that goes wrong:

```bash
scripts/Run-Win95-VM.sh --restore
```

That restoration is the environment's most important property. A 3dfx card in
*passthrough* mode takes control of the screen; a crash at the wrong moment leaves
the guest unusable, and without a quick return to a sound state, every failed
attempt would cost a complete reinstallation.

## The machine's configuration

Conforming to the project's target, subject to ADR 0002
([E00-S05](stories/E00-scoping/E00-S05-adr-hardware-target-glide.md)):

| Item | Value | Remark |
|---|---|---|
| Motherboard | Asus P2B-LS (`p2bls`) | 440BX chipset, the reference Pentium II platform |
| Processor | Pentium II Deschutes, 400 MHz | |
| Memory | 64 MB | |
| 2D video | S3 ViRGE/DX | the Voodoo 2 has no 2D output |
| 3D video | 3dfx Voodoo 2 | 4 MB of frame buffer, 4 MB of texture memory |
| Sound | Sound Blaster 16 | |
| Disks | 2 × IDE | C: system, D: transfer |

The choice of a separate 2D card is not an emulation artifice: it is the real
arrangement of a Voodoo 2, which plugs into the 2D card's output and takes over
only in full-screen 3D.

Every value is overridable by environment variable, which allows the other
configurations the ticket requires to be prepared:

```bash
# Voodoo 1, a single TMU - exercises E05-S04's multipass fallback
DKR_WIN95_VM=dkr-p1-voodoo1 DKR_WIN95_VOODOO_TYPE=0 \
DKR_WIN95_VOODOO_TEX=2 DKR_WIN95_VOODOO_FB=2 \
  scripts/Setup-Win95-TestVM.sh

# A low-end machine - exercises the performance floor
DKR_WIN95_VM=dkr-p2-slow DKR_WIN95_CPU_SPEED=233000000 DKR_WIN95_MEM_KB=32768 \
  scripts/Setup-Win95-TestVM.sh
```

## Transferring files

```bash
scripts/Push-To-Win95-VM.sh --dir DKRTEST build/DKR-R.exe
```

The file appears at `D:\DKRTEST\` in the guest. The write goes through `mtools`
directly into the FAT16 image, without mounting anything and without root
privileges.

Two points to watch:

- **The machine must be stopped** at the moment the guest is to see the result.
  Windows 95 caches the volume and will not re-read an image modified underneath
  it.
- **FAT16 imposes 8.3.** The script warns when a name will be truncated. It is the
  same constraint that bears on the save files
  ([E02-S05](stories/E02-system/E02-S05-eeprom-and-controller-pak-saves.md)).

## Known limits

The following **cannot** be validated here, and must go through E09-S04:

| Limit | Consequence |
|---|---|
| The Voodoo emulation is functional, not temporal | no graphics performance measurement transfers |
| The host runs the rendering far faster than period hardware | the fill budget cannot be measured here |
| Neither period PCI bandwidth nor period disk latency | texture downloads (E05-S02) and load times (E02-S04) are optimistic |
| Real 3dfx drivers, retail sound cards and controllers | compatibility to be checked on hardware |
| **The texel alpha arrives for `ARGB1555` and for no other 16-bit format** | the one-bit alpha stays; see below |

Everything **functional**, on the other hand, is validated here: binary format, PE
imports, startup, threads, display-list decoding, correctness of the rendering,
saves, input — with the one exception the last row names, which was found on
25 August 2026 and is the first *functional* limit this environment has shown.

`AI88.EXE` measures it directly. Three texture formats, the same quad, the same
combiner, the same blend:

| format | colour, drawn opaque | texel alpha, blended |
|---|---|---|
| `GR_TEXFMT_ARGB_1555` | arrives | arrives, and honours zero as well as one |
| `GR_TEXFMT_ALPHA_INTENSITY_88` | arrives — intensity in the low byte | **zero** |
| `GR_TEXFMT_ARGB_4444` | arrives exactly: `0x0F30` returns `FF3000` | **zero** |

Two sixteen-bit formats whose colour the card reads correctly and whose alpha is
zero at every value, against one whose alpha is right in both directions. The
`grTexCombine` and `grAlphaCombine` calls are identical across the three, so this
is not a translation fault, and the byte order is confirmed twice over by
swapping the bytes and watching the answer swap with them.

Whether the silicon behaves this way or 86Box's Voodoo simply does not implement
a multi-bit texel alpha cannot be settled here. Until it is, DKR's `IA` textures
keep the one-bit alpha `texture.c` gives them and its halos stay hard-edged.

## Current state

| Step | State |
|---|---|
| 86Box installed and runnable, without root privileges | ✅ |
| Machine POST: Pentium II 400 MHz, 65,536 KB, 3 disks detected | ✅ |
| Disk images partitioned and formatted from the host | ✅ |
| Windows 95 source extracted from the ISO onto E: | ✅ 63 files, 46 MiB |
| FreeDOS boot, C:/D:/E: visible to DOS | ✅ |
| Host ↔ guest transfer | ✅ in both directions |
| Driving without a screen (capture + key injection) | ✅ |
| **Windows 95 OSR2.5 installed and booting** | ✅ |
| Sound Blaster 16 detected by Windows | ✅ |
| Reference snapshot | ✅ `Run-Win95-VM.sh --snapshot` |
| **3dfx driver 3.01.00 installed and bound to the card** | ✅ "Voodoo2 3D Accelerator", with no warning |
| **Glide runtime in place** | ✅ `glide2x.dll`, `glide3x.dll`, `fxmemmap.vxd` in `C:\WINDOWS\SYSTEM` |
| **Glide demonstration** | ✅ 640×480 context, clears, buffer swaps, Gouraud triangle |

### Checking the card model — the costliest trap

**86Box did not apply the Voodoo settings written by hand into `86box.cfg`.** The
file said `type = 1`, `framebuffer_memory = 4`, `texture_memory = 4`; the settings
dialog showed a plain 3dfx Voodoo Graphics board, 2 MB + 2 MB. The machine
therefore emulated a **Voodoo 1** throughout the installation, while faithfully
keeping the configuration's text.

Nothing flags it, except by looking: the POST lists the card as `121A 0001` (the
Voodoo 1's identifier), and Glide eventually answers:

```text
_GlideInitEnvironment: glide2x.dll expected Voodoo, none detected
```

It is also the real reason `voodoo2.inf` did not recognise the card: it genuinely
was not a Voodoo 2.

**Check once through the dialog**, before any measurement:

```bash
86Box -S    # Display → Voodoo Graphics board → Configure
```

and choose "3Dfx Voodoo 2" there, 4 MB of frame buffer, 4 MB of texture memory.
(The dialog's labels appear in the interface's own language; they are given here
in English.) After that passage, the same values in the file are honoured, and
Windows re-detects new hardware at the next boot.

Consequence for
[E00-S05](stories/E00-scoping/E00-S05-adr-hardware-target-glide.md): **the emulated
card model must be verified in the dialog, not deduced from the configuration
file.** A texture budget measurement made on 2 MB instead of 4, or a multitexture
test made on a single TMU, would be wrong with nothing to flag it.

### The 3dfx driver: two traps

**The driver update wizard does not offer "Have Disk".** It filters the models by
the existing device's class, and an "unknown" device matches no class: it offers
nothing but "Unsupported device". The button is found in **Control Panel → Add New
Hardware**, which makes the INF be read directly.

**86Box exposes its Voodoo 2 with the Voodoo 1's PCI identifier.** The POST shows
it: `121A 0001` in the Device ID column, while `voodoo2.inf` binds only to
`PCI\VEN_121A&DEV_0002`. The original driver therefore cannot recognise the
emulated card, and auto-detection fails whatever path is given.

The fix: add the `DEV_0001` binding alongside the original one, at the three
places where the INF declares it (`[Mfg]`, the `Enum` key, and the description
strings). `scripts/patch_voodoo2_inf.py` does it.

To remember for
[E05-S01](stories/E05-glide/E05-S01-glide-init-and-buffers.md): run-time card
detection must not trust the PCI identifier alone, since it lies on this test
platform. It is `grGet` / `grSstQueryBoards` that are authoritative.

### Installing the 3dfx driver

The files are extracted into `C:\WINDOWS\TEMP` by the `V2_W9X_3.EXE` package, then
the INF is corrected and installed:

```bash
scripts/patch_voodoo2_inf.py --inf voodoo2.inf --output VOODOO2.INF
```

In the guest: **Control Panel → Add New Hardware → No → Sound, video and game
controllers → Have Disk → `C:\WINDOWS\TEMP` → Voodoo2 3D Accelerator**, then
reboot.

Verification from the host, with the machine stopped:

```bash
mdir -i win95.img@@32256 ::/WINDOWS/SYSTEM | grep -iE 'glide|fxmemmap'
```

`fxmemmap.vxd` is the kernel driver that maps the card's registers; without it,
`glide2x.dll` loads but opens no context.

### What was left: binding the 3dfx driver

The 3.01.00 driver's files are already extracted into `C:\WINDOWS\TEMP` —
`voodoo2.inf`, `glide2x.dll`, `glide3x.dll`, `3dfxv2.drv`, `fxmemmap.vxd`. The
card appears in Device Manager under **Other devices → PCI Multimedia Video
Device**, without a driver.

The driver update wizard **does not recognise the INF**: it answers that the
selected location contains no updated driver and, in manual selection, offers
nothing but "Unsupported device" with no "Have Disk" button. Three routes were
tried without success: automatic search, "Other locations" then *Finish* (the
procedure the driver's readme prescribes for OSR2), and `rundll32
setupx.dll,InstallHinfSection`.

The route that remains, and which exposes the "Have Disk" button the update wizard
lacks:

```text
Start → Settings → Control Panel → Add New Hardware
  → Next
  → "No" (do not search automatically)
  → choose the hardware type
  → "Have Disk..." → C:\WINDOWS\TEMP
  → "Voodoo2 3D Accelerator"
```

This takes a minute on a machine with a screen (`scripts/Run-Win95-VM.sh`). It can
also be driven by `scripts/Drive-Win95-VM.sh`, at the price of a longer keyboard
navigation.

### Glide demonstration

`tools/win95/glidetest.c` opens a 640×480 Glide context, chains three clears with
buffer swaps, then draws a Gouraud triangle. It writes its sequence of events into
`D:\GLIDETST.TXT`, readable from the host — proof independent of any screenshot.

```bash
tools/win95/build-glidetest.sh              # 32-bit PE, no CRT, no SSE
scripts/Push-To-Win95-VM.sh tools/win95/GLIDETST.EXE
# in the guest: d:\glidetst.exe
```

The binary imports only `kernel32` and `user32`, and loads Glide through
`LoadLibrary`: it depends on no redistributable and keeps mingw-w64's CRT startup
— the expected sticking point under Windows 95 — out of the equation. On that
count it also serves as the first witness for
[E00-S02](stories/E00-scoping/E00-S02-spike-pe-win95-toolchain.md).

The result obtained:

```text
glide2x.dll loaded
Glide symbols resolved
grGlideInit
3dfx boards detected: 1
640x480 context open, double buffered
grBufferClear + grBufferSwap x3
grDrawTriangle: Gouraud triangle
SUCCESS: the Glide stack works end to end
```

**A trap this demonstration found, and which holds for all of E05:** Glide 2.x's
`GrVertex` structure lays its fields out in the order `x, y, z, r, g, b, ooz, a,
oow` — `ooz` and `a` sit between the colours and `oow`. A "logical" structure
(`x, y, ooz, oow, r, g, b, a`) compiles without a warning and renders an
impeccable triangle **with permuted colours**: Glide simply reads the floats at
the wrong offsets. A red vertex comes out green. Nothing in the code, the compiler
or Glide flags it — only visual comparison catches it. It is
[E04-S08](stories/E04-hle-f3ddkr/E04-S08-reference-software-rasteriser.md)'s
argument in miniature.
