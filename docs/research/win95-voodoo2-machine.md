# The test machine was emulating a Voodoo 1, and why we had not seen it

A survey from
[E00-S05](../stories/E00-scoping/E00-S05-adr-hardware-target-glide.md),
14 August 2026.

## The file was not lying — we were not reading the right section

ADR 0002 concluded that `86box.cfg` "lied": Glide reported type `0` (Voodoo
Graphics) while the file announced `type = 2`. It was the third time we accused
it, and it was wrong every time.

There are **two Voodoo sections** in that file:

```ini
[3dfx Voodoo Graphics #1]     ← the one 86Box reads
type = 1

[3Dfx Voodoo Graphics]        ← written by hand, never read
type = 2
```

86Box reads the one carrying the **instance suffix**. It said `type = 1`, that is
*Obsidian SB50 + Amethyst* — a two-TMU Voodoo 1 — and 86Box honoured it
faithfully. Accusing the tool of lying cost two months during which the machine
was not the one we thought.

The enumeration reads out of 86Box's binary:

```
0  3Dfx Voodoo Graphics
1  Obsidian SB50 + Amethyst (2 TMUs)
2  3Dfx Voodoo 2
```

## The machine is now on the floor card

`type = 2`, 2 MB frame buffer, 2 MB texture memory — the ADR's **floor**
configuration, the one that puts the two tightest constraints to the test.
Confirmed in the settings dialog, the only authoritative source (its labels appear
in the interface's own language; they are given here in English):

```text
Voodoo type            : 3Dfx Voodoo 2
Frame buffer memory    : 2 MB
Texture memory         : 2 MB
```

E05-S02's texture budget will therefore be tried against the real limit, and not
against twice it as was the case with 4 MB per TMU.

## What Glide 2.54 says about it, and what it does not

| | Obsidian (before) | Voodoo 2 (after) |
|---|---|---|
| `type` | 0 | **0** |
| FBI revision | 261 | **261** |
| frame buffer | 4 MB | 2 MB |
| TMUs | 2 × 4 MB | 2 × 2 MB |

**Only the memory sizes change.** The type and the FBI revision are identical.

Glide 2.x's `GrSstType` does not separate the two generations:
`GR_SSTTYPE_VOODOO` covers the family, and `glide2x` 2.54 *is* the Voodoo 2
driver. It is therefore not an anomaly of the emulation but the shape of the API.

**Consequence for E05-S01:** run-time detection cannot rest on the type. The
number of TMUs and the memory per TMU are usable — and they are, moreover, the
only two things the engine needs in order to decide between one pass and two. The
exact model is not.

**A reservation on scope:** this report is the emulation's. On real hardware the
FBI revision differs between generations and could discriminate — to be checked in
E09-S04, and one more reason not to rest the detection on it.

## Reproducing

```sh
P=~/.local/dkr-win95
i686-w64-mingw32-gcc-posix -O2 -march=pentium2 -mno-sse -D_WIN32_WINNT=0x0400 \
  -nostdlib -nostartfiles -e _start -o GLIDEHW.EXE \
  tools/win95/probes/glide_hwinfo.c -lkernel32 -luser32

scripts/Push-To-Win95-VM.sh GLIDEHW.EXE    # then run it, read D:\GLIDEHW.TXT
```

And **check the model in the settings dialog**: Tools → Settings → Display →
Configure, opposite the Voodoo Graphics board. Never in the file.
