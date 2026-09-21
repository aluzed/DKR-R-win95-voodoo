/* The three DKR functions that genuinely use a general register as 64 bits.
 *
 * Each appears twice: once transcribed from what the recompiler emits, where
 * the temporaries live in general registers and so follow the `gpr` width, and
 * once with the wide locals a width-aware recompiler would emit.  At the
 * upstream width the two are the same computation; at the narrowed width only
 * the second one is still DKR.
 *
 * These are written against loose registers rather than a recomp_context so
 * that they can be tested without the runtime.  Folding them back in would go
 * through the `stubs` list in the recomp policy, which already exists for
 * replacing a generated function with a hand-written one.
 *
 * See docs/research/cpu-budget.md, "Narrowing the guest register, done
 * properly", for how these three were found and why there are no others.
 */

#ifndef DKR_WIDE_REGISTER_PATHS_H
#define DKR_WIDE_REGISTER_PATHS_H

#include "recomp.h"

/* A general register widened the way the upstream 64-bit `gpr` holds it: every
   value in it is the sign extension of a 32-bit quantity, so this is what
   `U64(ctx->rN)` yields there regardless of the register's declared width. */
#define DKR_AS_HELD(reg) ((uint64_t)(int64_t)(int32_t)(reg))

/* --- rand_range (funcs_26.c, 0x8006F94C) -----------------------------------
 * A 64-bit scramble of a 32-bit seed: two doubleword shifts past bit 31, two
 * shifts back, an or and an xor.  Under a 32-bit register the four `dsll32` and
 * `dsrl32` sites shift by 32 or more, which is undefined; gcc says so, which
 * makes this the one of the three the compiler finds by itself.
 */

#define DKR_RAND_SEED_ADDRESS 0x800DD434u

static gpr dkr_rand_scramble_as_emitted(gpr seed) {
    gpr r8 = seed, r9, r10, r11;
    r9 = r8 << (31 + 32);
    r10 = r8 << 31;
    r9 = r9 >> 31;
    r10 = r10 >> (0 + 32);
    r11 = r8 << (12 + 32);
    r9 = r9 | r10;
    r11 = r11 >> (0 + 32);
    r9 = r9 ^ r11;
    r11 = r9 >> 20;
    r11 = r11 & 0xFFF;
    return r11 ^ r9;
}

static gpr dkr_rand_scramble_wide(gpr seed) {
    const uint64_t r8 = DKR_AS_HELD(seed);
    uint64_t r9, r10, r11;
    r9 = r8 << (31 + 32);
    r10 = r8 << 31;
    r9 = r9 >> 31;
    r10 = r10 >> (0 + 32);
    r11 = r8 << (12 + 32);
    r9 = r9 | r10;
    r11 = r11 >> (0 + 32);
    r9 = r9 ^ r11;
    r11 = r9 >> 20;
    r11 = r11 & 0xFFF;
    /* The result is stored with a 32-bit `sw`, so it narrows here either way. */
    return (gpr)(int32_t)(uint32_t)(r11 ^ r9);
}

/* --- atan2s (funcs_9.c, 0x80070674) ----------------------------------------
 * A fixed-point divide: shift the numerator left by 11 to make room for the
 * fractional bits, divide, then use the low bits of the quotient as an index
 * into a 4096-entry table.  Under a 32-bit register the shift drops the top
 * eleven bits of the numerator and the index is unrelated to the angle.  This
 * one compiles without a warning at either width.
 */

static void dkr_atan2s_quotient_as_emitted(gpr numerator, gpr denominator,
                                           uint64_t* quotient,
                                           uint64_t* remainder) {
    gpr shifted = numerator << 11;
    DDIVU(U64(shifted), U64(denominator), quotient, remainder);
}

static void dkr_atan2s_quotient_wide(gpr numerator, gpr denominator,
                                     uint64_t* quotient, uint64_t* remainder) {
    const uint64_t shifted = DKR_AS_HELD(numerator) << 11;
    DDIVU(shifted, DKR_AS_HELD(denominator), quotient, remainder);
}

/* --- dmacopy_doubleword (funcs_14.c, 0x80070B04) ---------------------------
 * Sixteen bytes an iteration through two general registers.  Under a 32-bit
 * register every `ld` truncates and half the payload is replaced by the sign
 * extension of the other half.  Silent at compile time, and the copy is on the
 * path of every DMA the game does.
 */

static void dkr_dmacopy_as_emitted(uint8_t* rdram, gpr source, gpr destination,
                                   gpr end) {
    gpr r4 = source, r5 = destination, r6 = end, r8, r9;
    do {
        r8 = (gpr)LD(r4, 0x0);
        r9 = (gpr)LD(r4, 0x8);
        r5 = ADD32(r5, 0x10);
        r4 = ADD32(r4, 0x10);
        SD(r8, -0x10, r5);
        SD(r9, -0x8, r5);
    } while (r5 != r6);
}

static void dkr_dmacopy_wide(uint8_t* rdram, gpr source, gpr destination,
                             gpr end) {
    gpr r4 = source, r5 = destination, r6 = end;
    uint64_t t0, t1;
    do {
        t0 = LD(r4, 0x0);
        t1 = LD(r4, 0x8);
        r5 = ADD32(r5, 0x10);
        r4 = ADD32(r4, 0x10);
        SD(t0, -0x10, r5);
        SD(t1, -0x8, r5);
    } while (r5 != r6);
}

#endif /* DKR_WIDE_REGISTER_PATHS_H */
