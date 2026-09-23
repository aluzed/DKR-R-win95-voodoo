# The high-level audio mixer

E03-S03. `platform/audio/aspmain_hle.c` carries out DKR's audio command lists in
plain C, in place of the recompiled `aspMain` microcode. The Windows 95 target
uses it by default. `DKR_AUDIO_MICROCODE=1` selects the microcode instead.

## Why

On the test machine, a Pentium II at 400 MHz, the recompiled microcode costs
**695 ms of processor per second of sound**
(`docs/research/rsp-scalar-aliasing.md`). Real-time sound through it would take
70% of the machine before anything else ran. The microcode runs through
librecomp's scalar vector path, which emulates eight 16-bit lanes and a 48-bit
accumulator one lane at a time. The mixer does the same arithmetic directly.

## Fidelity: none lost

The ticket expected an accepted regression in fidelity. There is none on what
has been tested. **The mixer is bit-exact against the microcode.**

- `tools/audio/abi_difftest` runs each command on synthetic tasks, with random
  buffers, parameters, codebooks and states, through both implementations. It
  compares all of RDRAM and the state block. All eleven tests pass, over 200 to
  1,000 cases each.
- `tools/audio/replay_hle` replays audio tasks captured from the running game
  (`DKR_AUDIO_CAPTURE`) through both. On twelve tasks of 1,295 to 1,604
  commands, writing 10,877 to 14,644 bytes each, it finds 0 bytes of difference
  in RDRAM and 0 in the buffer area of DMEM.

The oracle is the recompiled microcode on the host's SIMD path. The Windows 95
target's scalar path matches it bit for bit since the strict-aliasing fix.

`tools/audio/build-host-tools.sh` builds every tool.

## DKR's audio ABI, as read from the microcode

It is SGI's ABI 1 `aspMain`, loaded at IMEM 0x1080. DMEM layout:

| DMEM | Contents |
|---|---|
| 0x000 | constants: `0, 1, 2, -1, 32, 2048, 0x7FFF, 0x4000` |
| 0x010 | the jump table, one halfword per command |
| 0x030 | ADPCM nibble masks and shift multipliers |
| 0x050 | ENVMIXER constants: lane ramps `0x2000 ... 0xE000, 0xFFFF` at 0xC0 |
| 0x0D0 | RESAMPLE's table: 64 entries of 4 taps |
| 0x320 | the segment table, 16 words, cleared at task start |
| 0x360 | the mixer's state: in, out, count, volumes, aux buffers, targets, rates, dry, wet |
| 0x380 | the command list, streamed in |
| 0x4C0 | the ADPCM codebook (LOADADPCM), also POLEF's table |
| 0x5C0 | the buffers: every buffer address in a command is relative to it |
| 0xF90 | scratch state for RESAMPLE, POLEF and ENVMIXER |
| 0xFC0 | the task |

The sixteen commands and what each computes are in the comments above each
function in `aspmain_hle.c`. The points that are not what one would guess:

- **DMA follows librecomp, not the hardware.** Only the RDRAM address is aligned
  to 8, and the length is exact. The oracle and the target both run
  librecomp's DMA.
- **SETLOOP writes its address at state + 0x10**, on top of SETVOL's left
  target and rate.
- **MIXER** computes `sat16((out * 0x7FFF * 2 + 0x8000 + in * gain * 2) >> 16)`,
  32 bytes at a time.
- **ADPCM** computes `sat16(S * 32 >> 16)`, where S wraps at 32 bits. The next
  frame's inputs are read before the current frame is stored.
- **RESAMPLE** rounds each tap with VMULF and sums the four pairwise, with
  saturation. The state carries this revision's flag-2 alignment remainder.
  The microcode's scratch vectors at 0xFB0 and 0xFC0 are reproduced.
- **POLEF** scales book1 by `gain * 4` and writes it back over the codebook.
  With A_INIT it clears only half of its state.
- **ENVMIXER** uses a linear ramp in 16.16 on eight lanes. The integer part is
  clamped to the target, with an unsigned minimum when rising and a signed
  maximum when falling. A continuing task takes its parameters from its saved
  state, not from SETVOL. An A_INIT task mixes at least two blocks.

## Known differences

Only in cases DKR's command lists do not contain:

- **MIXER and INTERLEAVE with partially overlapping buffers.** The microcode
  loads the next block before it stores the last, and the mixer processes
  sample by sample. DKR only ever mixes in place or between disjoint buffers.
  Both are exact, and they are what the tests cover.
- **RESAMPLE's flag 2 with a corrupt remainder in its state.** The microcode
  itself derails on the host. A real state's remainder is always even and below
  16, and DKR never sets flag 2.

## Cost

On the test machine, exclusive mode:

|  | microcode | mixer, first version | mixer, 32-bit |
|---|---:|---:|---:|
| per task | 26.7 ms | 12.4 ms | **8.4 ms** |
| processor per second of sound | 695 ms | 323 ms | **219 ms** |
| share for real-time sound | 70% | 32% | **22%** |

The "32-bit" version rewrites every product so that it fits in 32 bits. MIXER
becomes `(out*0x7FFF + in*gain + 0x4000) >> 15`, VMULF becomes
`(a*b + 0x4000) >> 15`, and the ADPCM and POLEF sums wrap in unsigned 32-bit
arithmetic before `>> 11` and `>> 14`. A 64-bit multiply is several instructions
on i686. The version also reads aligned halfwords in one load and copies DMA by
whole words. None of this changes a bit of output: abi_difftest and replay_hle
still match everywhere. On the host, where 64-bit multiplies are free, it makes
no difference at all. On the target it is a third off.

The frame in normal mode, with `DKR_RDRAM_SNAPSHOT=none` and `DKR_GFX_NO_STATS=1`:
52.7 ms with the microcode, then 45.1 ms, then **44.0 ms (22.7 fps)**.

On the target, the mixer's output matches the host's microcode oracle bit for bit
on a captured task: 11,006 bytes written, 0 mismatched.

`DKR_TRACE_AUDIO_ZONES=1` reports the mixer's cost per command on the cycle
counter (`[audio][zones]`).

### Word-wide sample loops

An aligned DMEM word is one native u32 holding two samples: the one at address a
in its high half, the one at a + 2 in its low half. MIXER, the ENVMIXER buffers,
RESAMPLE's output, its input taps and its table now move whole words, with a
fallback to the byte-exact path when a buffer is not word-aligned. DKR's buffers
always are aligned, to 16 bytes. Output is still bit-exact on all tests. On the
target, per task, measured with `DKR_TRACE_AUDIO_ZONES`:

| | before | after |
|---|---:|---:|
| whole mixer | 7.59 ms | **6.43 ms** |
| MIXER, per call | 11.1 µs | 4.2 µs |
| RESAMPLE, per call | 23.7 µs | 20.6 µs |
| ENVMIXER, per call | 50.9 µs | 48.0 µs |

ENVMIXER is now 39% of the mixer and is limited by arithmetic, not memory:
about 120 cycles per sample for four gains and four mixes. The frame in normal
mode, with both opt-in options, is **42.7 ms (23.4 fps)**.
