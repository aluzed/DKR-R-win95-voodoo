# The audio microcode computed the wrong sound on Windows 95

## What was found

E03-S03 needs an oracle for the high-level mixer to be measured against, and the
recompiled microcode is that oracle. To build it, audio tasks were captured on the
test machine and replayed on the host. `DKR_AUDIO_CAPTURE` saves DMEM and the low
4 MB of RDRAM before and after each task, and `tools/audio/replay_aspmain` replays
the task through the same `dkrAspMain`.

The host and the target disagreed on **95% of the bytes the task writes**. The
host's output buffer held a signal (8167, 13244, 6244...). The target's held
almost nothing (-3, -19, 18...). The reverb delay line arrived saturated at
±32767.

librecomp implements every vector instruction twice. There is a SIMD path, SSE4.1
or NEON, which every modern target runs, and a scalar "SISD" path, which only a
processor without SSE4.1 takes. The Pentium II is such a processor. Replayed on
the host through the scalar path, the task:

- also disagreed with the SIMD path, and agreed with the target on 1,076 of 1,696
  output samples;
- was **not deterministic**: two replays of the same task with the same binary
  wrote 9,722 and 9,727 bytes.

A differential test of the 17 vector instructions the microcode uses
(`tools/audio/vu_difftest.cpp`, 2,000 random trials per instruction and element
selector) found the two paths identical, once the flag registers are given real
masks. The instructions were right one at a time. Built with `-O1` and the
sanitizers, the scalar replay matched SIMD exactly, and neither sanitizer said a
word. The defect depended on the optimisation level.

## The cause

`RSP::r128` stores its lanes as `uint64_t u128[2]`, and the scalar path reaches
them through casts, `((uint16_t*)&u128)[7 - index]` and the like. Reading a
`uint64_t` through a `uint16_t*` breaks the strict-aliasing rule. At `-O2` and
above, GCC is entitled to reorder loads and stores across such accesses, and it
does. A load moved ahead of the store that should feed it reads a stale value
from the stack, which also explains why the result changed from run to run. The
SIMD path goes through intrinsics, whose vector types may alias, and is
unaffected. So nobody upstream could see it.

The target compiled `RecompiledRSP/*.cpp` at `-O3`, with strict aliasing.

## The fix

`-fno-strict-aliasing` on the RSP sources of the Windows 95 target
(`cmake/win95-target.cmake`). On the host, the scalar path then matches the SIMD
path bit for bit and is deterministic. On the test machine, six tasks captured
after the fix:

    AUD000  task wrote 11102 bytes   mismatched 0
    AUD001            10877          0
    AUD002            11448          3   other writers 7994
    AUD003            11248          0
    AUD004            11271          1   other writers 11747
    AUD005            11900          5

The few remaining bytes are in captures where other threads wrote RDRAM while the
task ran, 7,994 and 11,747 bytes in the two largest cases. The capture races the
game; the microcode does not.

## What it changes

- **Every audio figure measured before this fix measured a wrong computation.** The
  cost per task had the right order of magnitude, since it was the same code, but
  it has to be measured again.
- The game's recompiled code (`RecompiledFuncs/*.c`) was built at `-O3` with
  strict aliasing too. N64Recomp's output requires `-fno-strict-aliasing`: its
  `MEM_W`, `MEM_H` and `MEM_B` macros reach one RDRAM through `int32_t*`,
  `int16_t*` and `int8_t*`. The modern targets pass the option
  (`runtime-recomp/CMakeLists.txt`). The Windows 95 target rebuilt the library
  itself and left it out. It now passes it for the whole `win95recompiled`
  library. The cost was measured in normal mode with both opt-in options: 43.2 ms
  against 42.7 ms, about 1%, at the edge of the run-to-run spread. No defect had
  been traced to it; the point is not to wait for one.

## The tools

`tools/audio/build-host-tools.sh` builds all of these into `build/audio-tools`:

- `replay_aspmain` and `replay_aspmain_sisd`: replay captures through each path
  and compare the bytes the task wrote. `REPLAY_DUMP=1` saves the host's RDRAM.
- `vu_difftest_simd` and `vu_difftest_sisd`: diff their output with each other.
- `dump_alist.py`: prints a capture's command list, `--summary` for counts and
  flags.

## The corrected cost

Measured again after the fix, exclusive mode, 80.8 s, 1,090 tasks:

    per task                        26.7 ms    (24.2 before the fix)
    processor per second of sound    695 ms    (628)
    share of the processor          36.1%

The correct computation costs about 10% more than the broken one did. Real-time
sound through the microcode would now take **70% of the processor**. E03-S03 is
more necessary than before, not less.
