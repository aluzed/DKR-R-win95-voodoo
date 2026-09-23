/* E03-S03 - a high-level interpreter for DKR's audio microcode.
 *
 * DKR's `aspMain` is SGI's ABI 1 audio microcode: sixteen commands, a jump table
 * at DMEM 0x10, the command list streamed into DMEM 0x380, a segment table at
 * 0x320, the mixer's state at 0x360, the ADPCM codebook at 0x4C0 and the sample
 * buffers from 0x5C0. This file carries out the same commands in plain C
 * instead of running the recompiled microcode instruction by instruction, which
 * on a Pentium II costs 628 ms of processor per second of sound.
 *
 * It works on the same memories the microcode does, laid out the same way, so
 * that it can replace `dkrAspMain` without the rest of the runtime noticing:
 * `dmem` is the RSP's 4 KB data memory as librecomp holds it, the task already
 * at 0xFC0 and the microcode's data at 0x000; `rdram` is the runtime's RDRAM,
 * indexed by physical address. Both store each big-endian word as one native
 * little-endian word, so a byte at address a lives at a ^ 3 and an aligned
 * halfword at a ^ 2.
 *
 * The reference is the recompiled microcode itself (tools/audio): every command
 * is compared against it, and where this file departs from it the difference is
 * recorded in docs/AUDIO-HLE.md.
 */
#ifndef DKR_ASPMAIN_HLE_H
#define DKR_ASPMAIN_HLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the task at DMEM 0xFC0. Returns the number of commands executed. */
unsigned long dkr_aspmain_hle(unsigned char *rdram, unsigned char *dmem);

#ifdef __cplusplus
}
#endif

#endif /* DKR_ASPMAIN_HLE_H */
