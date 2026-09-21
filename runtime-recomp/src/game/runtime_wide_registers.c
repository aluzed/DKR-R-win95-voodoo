/* The three DKR functions that use a general register as sixty-four bits.
 *
 * On a target that defines DKR_NARROW_GUEST_REGISTER the register file is
 * 32 bits wide, which is right for 116,782 of the game's 116,795 operations
 * and wrong for thirteen. Those thirteen live in three functions, and this
 * file is where they get their width back. The recompilation policy hooks each
 * function at its entry and these take over, so the generated bodies are never
 * reached.
 *
 * They are compiled on **every** target, narrow or wide. At the upstream width
 * they compute what the generated code computes -- `tools/cpu-budget/
 * narrow_gpr_test.c` checks that by running both against each other -- so there
 * is no second code path to keep in step, and no build where these are dead.
 *
 * Each is a transcription of the emitted function rather than a rewrite: the
 * same registers in the same order, including the ones nothing reads
 * afterwards, because the point is fidelity and not economy. The only liberty
 * taken is that a value the guest holds in a register across a doubleword
 * operation is held here in a local of the width that operation asks for.
 *
 * docs/research/cpu-budget.md records the census that says there is no fourth
 * such function, and how each of these three fails when it does not get its
 * width: the block copy on every call, the generator to a constant, and the
 * arctangent only past 2^21 -- which is the one that would survive a play-test.
 */

#include "recomp.h"

#include <stdint.h>

/* A general register widened the way a 64-bit `gpr` holds it. Every value in
   the guest's register file is the sign extension of a 32-bit quantity, so this
   is what `U64(ctx->rN)` yields at either width. */
static uint64_t as_held(gpr value) {
    return (uint64_t)(int64_t)(int32_t)value;
}

/* dmacopy_doubleword, 0x80070B04.
 *
 * Sixteen bytes an iteration through $t0 and $t1. Narrowed, every `ld`
 * truncates and half the payload is replaced by the sign extension of the
 * other half -- on the path of every DMA the game does. */
int dkr_wide_dmacopy_doubleword(uint8_t* rdram, recomp_context* ctx) {
    uint64_t t0, t1;
    do {
        t0 = LD(ctx->r4, 0x0);
        t1 = LD(ctx->r4, 0x8);
        ctx->r5 = ADD32(ctx->r5, 0x10);
        ctx->r4 = ADD32(ctx->r4, 0x10);
        SD(t0, -0x10, ctx->r5);
        SD(t1, -0x8, ctx->r5);
    } while (ctx->r5 != ctx->r6);
    ctx->r8 = (gpr)t0;
    ctx->r9 = (gpr)t1;
    return 1;
}

/* rand_range, 0x8006F94C.
 *
 * A 64-bit scramble of a 32-bit seed, then a remainder to fold it into the
 * caller's range. Narrowed, the four `dsll32`/`dsrl32` shift by 32 or more,
 * which is undefined on a 32-bit type; gcc folds them to zero and the generator
 * returns zero for every seed. This is the one of the three the compiler finds
 * by itself, under -Wshift-count-overflow. */
int dkr_wide_rand_range(uint8_t* rdram, recomp_context* ctx) {
    ctx->r8 = (gpr)S32(0x800E << 16);
    ctx->r8 = MEM_W(ctx->r8, -0x2BCC);
    ctx->r1 = (gpr)S32(0x800E << 16);
    ctx->r5 = SUB32(ctx->r5, ctx->r4);

    const uint64_t seed = as_held(ctx->r8);
    uint64_t r9 = seed << (31 + 32);
    uint64_t r10 = seed << 31;
    r9 = r9 >> 31;
    r10 = r10 >> (0 + 32);
    uint64_t r11 = seed << (12 + 32);
    r9 = r9 | r10;
    r11 = r11 >> (0 + 32);
    r9 = r9 ^ r11;
    r11 = r9 >> 20;
    r11 = r11 & 0xFFF;
    ctx->r9 = (gpr)r9;
    ctx->r10 = (gpr)r10;
    ctx->r11 = (gpr)r11;
    ctx->r8 = (gpr)(r11 ^ r9);

    MEM_W(-0x2BCC, ctx->r1) = (int32_t)ctx->r8;
    ctx->r5 = ADD32(ctx->r5, 0x1);
    ctx->r8 = SUB32(ctx->r8, ctx->r4);

    /* The guest divides before it checks, and traps on the delay slot. */
    if (U32(ctx->r5) == 0) {
        do_break(0x8006F9AC);
        return 1;
    }
    const gpr quotient = (gpr)S32(U32(ctx->r8) / U32(ctx->r5));
    const gpr remainder = (gpr)S32(U32(ctx->r8) % U32(ctx->r5));
    ctx->r8 = quotient;
    ctx->r2 = remainder;
    ctx->r2 = ADD32(ctx->r2, ctx->r4);
    return 1;
}

/* atan2s, 0x8007066C.
 *
 * Folds the pair into one octant, then divides the smaller by the larger with
 * eleven fractional bits and reads the angle out of a 4096-entry table. The
 * numerator is shifted left by eleven, so narrowed it keeps the right answer
 * while the numerator fits in twenty-one bits and loses it one step later:
 *
 *     2097151/3 -> index 0x2aa    correct
 *     2097152/3 -> index 0x000    wrong
 *
 * Coordinate differences are mostly small, so a narrowed build would compute
 * correct angles almost everywhere and wrong ones at the far end of a track.
 * This one compiles without a warning at either width. */
int dkr_wide_atan2s(uint8_t* rdram, recomp_context* ctx) {
    ctx->r8 = ctx->r4 | ctx->r5;
    if (ctx->r8 == 0) {
        ctx->r2 = ADD32(0, 0x0);
        return 1;
    }

    if (SIGNED(ctx->r4) < 0) {
        ctx->r4 = SUB32(0, ctx->r4);
        if (SIGNED(ctx->r5) < 0) {
            ctx->r5 = SUB32(0, ctx->r5);
            ctx->r2 = 0 | 0x8000;
        } else {
            ctx->r2 = 0 | 0xC000;
            /* $a0 and $a1 swap, so the octant fold always divides the smaller
               of the two by the larger. */
            const gpr swap = ctx->r4;
            ctx->r4 = ctx->r5;
            ctx->r5 = swap;
        }
    } else {
        if (SIGNED(ctx->r5) < 0) {
            ctx->r5 = SUB32(0, ctx->r5);
            ctx->r2 = ADD32(0, 0x4000);
            const gpr swap = ctx->r4;
            ctx->r4 = ctx->r5;
            ctx->r5 = swap;
        } else {
            ctx->r2 = ADD32(0, 0x0);
        }
    }

    ctx->r8 = SUB32(ctx->r4, ctx->r5);
    const int numerator_is_r4 = SIGNED(ctx->r8) < 0;

    uint64_t quotient = 0, remainder = 0;
    ctx->r9 = (gpr)S32(0x800E << 16);
    ctx->r9 = ADD32(ctx->r9, -0x23C2);

    if (numerator_is_r4) {
        if (ctx->r5 == 0) {
            do_break(0x8007072C);
            return 1;
        }
        DDIVU(as_held(ctx->r4) << 11, as_held(ctx->r5), &quotient, &remainder);
        ctx->r8 = (gpr)quotient;
        ctx->r8 = ctx->r8 & 0xFFE;
        ctx->r9 = ADD32(ctx->r9, ctx->r8);
        ctx->r8 = MEM_H(ctx->r9, 0x0);
        ctx->r2 = ADD32(ctx->r2, ctx->r8);
    } else {
        ctx->r2 = ADD32(ctx->r2, 0x4000);
        if (ctx->r4 == 0) {
            do_break(0x800706F0);
            return 1;
        }
        DDIVU(as_held(ctx->r5) << 11, as_held(ctx->r4), &quotient, &remainder);
        ctx->r8 = (gpr)quotient;
        ctx->r8 = ctx->r8 & 0xFFE;
        ctx->r9 = ADD32(ctx->r9, ctx->r8);
        ctx->r8 = MEM_H(ctx->r9, 0x0);
        ctx->r2 = SUB32(ctx->r2, ctx->r8);
    }

    ctx->r2 = ctx->r2 & 0xFFFF;
    return 1;
}
