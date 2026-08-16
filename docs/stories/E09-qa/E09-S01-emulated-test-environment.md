# E09-S01 — Emulated test environment

| | |
|---|---|
| **Epic** | E09 — Integration, QA and distribution |
| **Status** | REVIEW |
| **Priority** | P0 |
| **Estimate** | M |
| **Depends on** | — |
| **Blocks** | E00-S02, E05-S01, E09-S02 |

## State as of 2026-08-11

**The environment is operational end to end**: Windows 95 OSR2.5 on a Pentium II /
Voodoo 2, 3dfx driver installed, and a Glide demonstration that renders a Gouraud
triangle on screen. The machine is driven with no physical display.
Recipe: [`docs/TEST-ENVIRONMENT.md`](../../TEST-ENVIRONMENT.md).

| Step | State |
|---|---|
| 86Box v6.0 installed and runnable, without root | ✅ |
| Machine POSTs: Pentium II 400 MHz, 65,536 KB, 3 disks detected | ✅ verified by screen capture |
| Disk images partitioned and formatted from the host | ✅ |
| Windows 95 source extracted from the player's ISO onto E: | ✅ 63 files, 46 MiB |
| FreeDOS boot, C:/D:/E: visible from DOS | ✅ proved by a witness written from the guest |
| Host ↔ guest transfer through `mtools`, without root | ✅ in both directions |
| Driving with no display: capture + key injection | ✅ `Drive-Win95-VM.sh` |
| **Windows 95 OSR2.5 installed and starts** | ✅ ScanDisk with no error on C:, D:, E: |
| Sound Blaster 16 detected by Windows | ✅ |
| Snapshot / restore | ✅ `Run-Win95-VM.sh --snapshot` / `--restore` |
| Voodoo 1 and slow-machine configurations | ✅ through environment variables |
| **3dfx driver 3.01.00 installed and bound to the card** | ✅ "Voodoo2 3D Accelerator", with no warning |
| **Glide runtime in place** | ✅ `glide2x.dll`, `glide3x.dll`, `fxmemmap.vxd` in `C:\WINDOWS\SYSTEM` |
| **Glide demonstration** | ✅ 640×480 context, clears, swaps, Gouraud triangle |

Log written by the demonstration from the guest:

```text
glide2x.dll loaded / Glide symbols resolved / grGlideInit
3dfx boards detected: 1
640x480 context open, double buffered
grBufferClear + grBufferSwap x3
grDrawTriangle: Gouraud triangle
SUCCESS: the Glide stack works end to end
```

Delivered: `Setup-Win95-TestVM.sh`, `prepare_win95_install.py`, `patch_voodoo2_inf.py`,
`Run-Win95-VM.sh`, `Drive-Win95-VM.sh`, `Push-To-Win95-VM.sh`,
`tools/win95/azerty_keys.py`, `tools/win95/glidetest.c` + `build-glidetest.sh`,
`docs/TEST-ENVIRONMENT.md`.

### What the assembly taught

**Driving with no display is acquired**, and it is worth far more than the
installation: it is the foundation of
[E09-S02](E09-S02-visual-comparison-harness.md)'s visual comparison harness, which will
therefore be able to run in continuous integration. Two traps condition it, both
documented: `xdotool key --window` goes through `XSendEvent`, which Qt ignores, and
86Box routes the keyboard to the guest only after a capture click in its window.

**Diagnosis by screen capture was decisive.** Four boot attempts failed without leaving
any usable trace; the capture showed the cause in one image — the BIOS was waiting for
`Press F1 to continue` on a blank CMOS. From the outside, a machine stuck in the BIOS
and a machine that does not boot are indistinguishable.

**Four assembly errors found and corrected**, each invisible otherwise: a DOS hard disk
requires a partition table — a raw FAT volume is not seen; the system partition must
carry the active flag, failing which Windows would install without being able to start;
86Box resets to `none` any configuration value it refuses, which had silently removed
the 2D card (`virge375_pci`, and not `s3_virge_375_pci`) — with no video card, the
machine does not POST; and above all the disk geometry, below.

**The geometry trap, which cost the most.** A BIOS of the period addresses only 1024
cylinders; beyond that it doubles the heads until it comes back under the limit and
presents **that** translated geometry to `INT 13h`. Windows 95's boot code converts its
addresses to CHS with the number of heads written in the boot sector's BPB. Written
from Linux with 16 heads while the BIOS presented 64, every read fell wide of the mark:
the machine loaded anything at all and froze **without the slightest message**, exactly
like a non-bootable disk.

The POST did report it, though, in one column: `Pri. Master: LBA` against
`Sec. Slave: CHS`. Only the system disk exceeded 1024 cylinders, and it was the only one
that did not start. `prepare_win95_install.py` now computes the translated geometry
**before** creating the images.

Set aside along the way, each of them tested: boot-time virus detection, the boot
sequence, `Halt On: All Errors`, the presence of the floppy drive, and rewriting the MBR
with `FDISK /MBR`.

**The machine emulated a Voodoo 1 throughout the installation.** 86Box did not apply
the Voodoo settings written by hand in `86box.cfg`: the file said `type = 1` and 4 MB,
the dialogue displayed "3dfx Voodoo Graphics" and 2 MB. It kept the text while emulating
something else. Symptoms: the POST listed `121A 0001`, and Glide answered "expected
Voodoo, none detected". Corrected by going once through the settings dialogue; the same
values are honoured thereafter.

**That point goes back to [E00-S05](../E00-scoping/E00-S05-adr-hardware-target-glide.md):**
the card model must be verified in the dialogue, never deduced from the file. A texture
memory budget measured on 2 MB instead of 4, or a multitexture test conducted on a
single TMU, would be wrong with nothing to say so.

**A Glide trap found by the demonstration, and one that holds for all of E05.** Glide
2.x's `GrVertex` structure lays its fields out in the order
`x, y, z, r, g, b, ooz, a, oow`: `ooz` and `a` are interleaved between the colours and
`oow`. A "logical" structure compiles with no warning and renders an impeccable triangle
**with the colours permuted** — a red vertex comes out green. Neither the compiler nor
Glide reports anything at all; only the visual comparison catches it. It is
[E04-S08](../E04-hle-f3ddkr/E04-S08-reference-software-rasteriser.md)'s argument in
miniature.

**The 3dfx driver required two corrections.** The driver update wizard does not expose
"Have Disk" — it filters the models by the existing device's class, and an unknown
device matches no class. It is **Control Panel → Add New Hardware** that makes it read
the INF. And above all: **86Box exposes its Voodoo 2 with the Voodoo 1's PCI identifier**
(`121A:0001`), whereas `voodoo2.inf` binds only to `DEV_0002`. The original driver
therefore cannot recognise the emulated card. `scripts/patch_voodoo2_inf.py` adds the
missing binding.

A direct consequence for
[E05-S01](../E05-glide/E05-S01-glide-init-and-buffers.md): **card detection at run time
must not rely on the PCI identifier**, which lies on this test platform. A naive
detection would believe it was dealing with a single-TMU Voodoo 1 and would take
E05-S04's multipass fallback for no reason. `grSstQueryBoards` and `grGet` are
authoritative.

**An answer for the graphics backlog.** The 3dfx driver 3.01.00 carries **Glide 2.54 and
Glide 3.01** for the Voodoo 2: the choice of API version in
[E00-S05](../E00-scoping/E00-S05-adr-hardware-target-glide.md) therefore does not depend
on the hardware — both runtimes are on the machine.

The card model, on the other hand, is nowhere reliably readable: the device manager
displays "Hardware version: 002", which is the **revision** and not the identifier, and
the PCI identifier says `0001`, the Voodoo 1's. Only the installed driver — "Voodoo2 3D
Accelerator" — attests to the model. All the more reason for E05-S01 to interrogate
Glide rather than the bus.

**On the ISO supplied:** it is not bootable (no El Torito), and the French OSR2.5 CD has
no `SETUP.EXE` — its installer is called `INSTALL.EXE`. Hence the FreeDOS floppy and the
source placed on a hard disk rather than on the CD, which removes any dependency on a
DOS CD-ROM driver.

86Box placed an S3 ViRGE 2D card alongside the Voodoo 2: that is not an emulation
artifice but the real assembly of a Voodoo 2, which plugs into the 2D card's output and
takes over only in full-screen 3D — which confirms
[E06-S01](../E06-platform/E06-S01-win32-window-and-message-loop.md)'s integration
hypothesis.

## Context

This ticket has to be done **early**, before almost everything else: it is what makes
the project practicable. Without a test machine that is quick to reset, every check goes
through real hardware, and the debugging cycle becomes so slow that it discourages
experiment — exactly when the project demands the most of it, notably on Glide
(E05-S01).

Two emulators are suitable, and both emulate a Voodoo:

- **PCem** and its fork **86Box**, which emulate machines of the period at component
  level, with 3dfx cards.

Voodoo emulation is not perfect, and that is a limit to be aware of: it will suffice for
functional debugging, not for validating performance. E09-S04's real-hardware validation
stays indispensable; the emulator does not replace it, it makes it rare.

## Objective

To deliver a reproducible test environment: Windows 95 with an emulated 3dfx card,
installable and quick to reset.

## Scope

**In:** the emulated machine, its configuration, file transfer, the recipe.

**Out:** real hardware (E09-S04).

## Work

1. Choose the emulator on a concrete criterion: the quality of the Voodoo emulation, and
   the presence of the Glide drivers.
2. Configure a machine conforming to E00-S05's ADR: processor, memory, 3dfx card, sound
   card, disk.
3. Install Windows 95 OSR2.5, the 3dfx drivers, DirectX, and the version of `msvcrt`
   retained by E01-S03.
4. Save the state as a reference image, restorable in a few seconds. It is this
   environment's most important property: a Glide crash in full screen can leave the
   system unusable, and without a quick restore, every failed attempt costs a
   reinstallation.
5. Automate file transfer between the development host and the emulated machine. A
   virtual disk mounted on both sides is the simplest means. This point decides the speed
   of the debugging cycle: it deserves time spent on it.
6. Write a script that builds, transfers and launches in one command.
7. Prepare several configurations: a single-TMU Voodoo 1, a two-TMU Voodoo 2, and a
   slower machine, in order to exercise E05-S04's fallbacks and the performance limits.
8. Document the recipe in `docs/TEST-ENVIRONMENT.md`, precisely enough for a third party
   to rebuild the environment.

## Acceptance criteria

- [ ] A Windows 95 machine with an emulated 3dfx starts and runs a Glide demonstration.
- [ ] The reference state restores in a few seconds.
- [ ] File transfer is automated.
- [ ] One command builds, transfers and launches.
- [ ] At least three distinct configurations are available.
- [ ] `docs/TEST-ENVIRONMENT.md` allows a third party to rebuild the environment.
- [ ] The limits of the Voodoo emulation are documented, in particular what cannot be
      validated there.

## Risks

Voodoo emulation is approximate on certain points, and it would be expensive to chase a
defect that exists only in the emulator. Hence the importance of documenting its known
limits, and of confronting real hardware (E09-S04) at regular intervals rather than once
at the end.

## References

- PCem · 86Box — emulation of period machines with 3dfx cards
- E00-S05 — target hardware configuration
- E09-S04 — validation on real hardware
