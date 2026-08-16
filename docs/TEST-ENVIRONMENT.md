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

Everything **functional**, on the other hand, is validated here: binary format, PE
imports, startup, threads, display-list decoding, correctness of the rendering,
saves, input.

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
