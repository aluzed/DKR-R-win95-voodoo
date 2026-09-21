/* Do the native wide paths compute what the recompiled functions compute?
 *
 * The three functions in runtime_wide_registers.cpp are transcriptions of what
 * the recompiler emits, not rewrites, and this is what says so. It runs each
 * generated function and its native replacement over a corpus of inputs and
 * compares the whole guest register file and the memory each touched.
 *
 * Built twice, like narrow_gpr_test:
 *
 *   at the upstream width  the generated function and the native one must agree
 *                          on every input, which is what makes the replacement
 *                          safe to apply on every target rather than only the
 *                          narrow one;
 *   at the narrowed width  the generated functions are broken by construction,
 *                          so only the native results are compared -- across
 *                          builds, through the transcript hash.
 *
 * The generated bodies are extracted from RecompiledFuncs by the measurement
 * script rather than copied here, so this cannot drift from what ships.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "recomp.h"

#define GUEST_BASE   0x80100000u
#define RDRAM_SIZE   (8u * 1024u * 1024u)
#define SEED_ADDRESS 0x800DD434u
#define TABLE_BASE   0x800DDC3Eu

void generated_atan2s(uint8_t* rdram, recomp_context* ctx);
void generated_dmacopy_doubleword(uint8_t* rdram, recomp_context* ctx);
void generated_rand_range(uint8_t* rdram, recomp_context* ctx);

int dkr_wide_atan2s(uint8_t* rdram, recomp_context* ctx);
int dkr_wide_dmacopy_doubleword(uint8_t* rdram, recomp_context* ctx);
int dkr_wide_rand_range(uint8_t* rdram, recomp_context* ctx);

static unsigned long long transcript = 1469598103934665603ull;
static int divergences = 0;

static unsigned long long fold(unsigned long long hash, const void* bytes,
                               size_t count) {
    const unsigned char* p = (const unsigned char*)bytes;
    for (size_t i = 0; i < count; i++) {
        hash ^= p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static void record(const char* label, unsigned long long value) {
    transcript = fold(transcript, label, strlen(label));
    transcript = fold(transcript, &value, sizeof(value));
}

/* Everything the guest can see afterwards: the register file, and the region
   of memory the function under test is allowed to touch. */
static unsigned long long outcome(const recomp_context* ctx,
                                  const uint8_t* rdram, unsigned int address,
                                  size_t count) {
    unsigned long long hash = 1469598103934665603ull;
    const gpr* registers = &ctx->r0;
    for (int i = 0; i < 32; i++) {
        /* Compared as 32-bit quantities: the two widths hold the same guest
           value, one of them with its sign extension attached. */
        const uint32_t value = (uint32_t)registers[i];
        hash = fold(hash, &value, sizeof(value));
    }
    if (count != 0) {
        hash = fold(hash, rdram + (address - 0x80000000u), count);
    }
    return hash;
}

static void prepare(uint8_t* rdram) {
    /* A table of plausible sines, and a memory pattern whose doublewords have
       a high word that is not the sign extension of their low word. */
    for (unsigned int i = 0; i < 4096; i += 2) {
        const int16_t entry = (int16_t)((i * 7919u) & 0x3FFF);
        const unsigned int at = (TABLE_BASE + i) - 0x80000000u;
        rdram[at ^ 2] = (uint8_t)(entry >> 8);
        rdram[(at + 1) ^ 2] = (uint8_t)entry;
    }
    for (size_t i = 0; i < 4096; i++) {
        rdram[(GUEST_BASE - 0x80000000u) + i] = (uint8_t)(0xA0 + i * 31u);
    }
}

typedef void (*generated_function)(uint8_t*, recomp_context*);
typedef int (*native_function)(uint8_t*, recomp_context*);

/* Run both implementations from the same starting state and say whether they
   left the guest in the same one. */
static void compare(const char* what, uint8_t* rdram, const recomp_context* in,
                    generated_function generated, native_function native,
                    unsigned int watched, size_t watched_bytes,
                    uint32_t seed_value) {
    recomp_context left = *in, right = *in;
    unsigned long long from_generated, from_native;

    if (seed_value != 0) {
        MEM_W(0, (gpr)(int32_t)SEED_ADDRESS) = (int32_t)seed_value;
    }
    memset(rdram + (watched - 0x80000000u), 0, watched_bytes);
    generated(rdram, &left);
    from_generated = outcome(&left, rdram, watched, watched_bytes);

    if (seed_value != 0) {
        MEM_W(0, (gpr)(int32_t)SEED_ADDRESS) = (int32_t)seed_value;
    }
    memset(rdram + (watched - 0x80000000u), 0, watched_bytes);
    native(rdram, &right);
    from_native = outcome(&right, rdram, watched, watched_bytes);

    record(what, from_native);

    if (sizeof(gpr) > 4 && from_generated != from_native) {
        printf("  DIVERGED  %-28s generated %016llx  native %016llx\n",
               what, from_generated, from_native);
        divergences++;
    }
}

static recomp_context blank(void) {
    recomp_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    return ctx;
}

static void exercise_atan2s(uint8_t* rdram) {
    static const int32_t coordinates[] = {
        0, 1, -1, 2, -3, 100, -100, 4096, -4096, 32767, -32768,
        0x00100000, -0x00100000, 0x001FFFFF, 0x00200000, -0x00200000,
        0x00FFFFFF, 0x7FFFFFFF,
        /* INT32_MIN is deliberately absent; see exercise_atan2s_domain_limit. */
    };
    const size_t count = sizeof(coordinates) / sizeof(coordinates[0]);
    char label[64];
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < count; j++) {
            recomp_context ctx = blank();
            ctx.r4 = (gpr)(int32_t)coordinates[i];
            ctx.r5 = (gpr)(int32_t)coordinates[j];
            snprintf(label, sizeof(label), "atan2s %d,%d",
                     (int)coordinates[i], (int)coordinates[j]);
            compare(label, rdram, &ctx, generated_atan2s, dkr_wide_atan2s,
                    GUEST_BASE, 0, 0);
        }
    }
    printf("  atan2s          %zu pairs\n", count * count);
}

/* atan2s folds its pair into one octant by negating whichever coordinate is
   negative, which works for every 32-bit value except INT32_MIN, whose negation
   is itself. Pair INT32_MIN with zero and the fold leaves zero as the divisor.
   The guest traps there: `ddivu` with a zero divisor is followed by a `break`,
   and that is what the native path reaches. The generated code reaches it too,
   but only after the C division has already faulted on the host -- N64Recomp's
   DDIVU divides before the guest's check runs, so the recompiled function dies
   where the guest would have trapped.

   That is a defect in the recompilation of a function DKR never calls with such
   a coordinate, not in the transcription, and it is why the corpus above stops
   at INT32_MIN rather than including it. Only the native path is run here. */
static void exercise_atan2s_domain_limit(uint8_t* rdram) {
    recomp_context ctx = blank();
    ctx.r4 = (gpr)(int32_t)0;
    ctx.r5 = (gpr)(int32_t)0x80000000;
    puts("  atan2s at INT32_MIN, native path only:");
    dkr_wide_atan2s(rdram, &ctx);
    record("atan2s domain limit", outcome(&ctx, rdram, GUEST_BASE, 0));
}

static void exercise_rand_range(uint8_t* rdram) {
    static const uint32_t seeds[] = {
        1u, 0x12345678u, 0x80000000u, 0xFFFFFFFFu, 0x7A3B91C4u, 0xDEADBEEFu,
        0x00010001u, 0x55555555u,
    };
    static const int32_t ranges[][2] = {
        { 0, 1 }, { 0, 10 }, { 1, 6 }, { -5, 5 }, { 0, 0x7FFF },
        { -100, -1 }, { 0, 0x7FFFFFFE },
    };
    char label[64];
    size_t done = 0;
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        for (size_t j = 0; j < sizeof(ranges) / sizeof(ranges[0]); j++) {
            recomp_context ctx = blank();
            ctx.r4 = (gpr)(int32_t)ranges[j][0];
            ctx.r5 = (gpr)(int32_t)ranges[j][1];
            snprintf(label, sizeof(label), "rand_range %08x %d..%d",
                     seeds[i], (int)ranges[j][0], (int)ranges[j][1]);
            /* The seed lives in memory and the function advances it, so the
               watched region is the seed word itself. */
            compare(label, rdram, &ctx, generated_rand_range,
                    dkr_wide_rand_range, SEED_ADDRESS, 4, seeds[i]);
            done++;
        }
    }
    printf("  rand_range      %zu cases\n", done);
}

static void exercise_dmacopy(uint8_t* rdram) {
    static const size_t lengths[] = { 16, 32, 64, 128, 512, 4096 };
    char label[64];
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        const unsigned int target = GUEST_BASE + 0x10000u;
        recomp_context ctx = blank();
        ctx.r4 = (gpr)(int32_t)GUEST_BASE;
        ctx.r5 = (gpr)(int32_t)target;
        ctx.r6 = (gpr)(int32_t)(target + lengths[i]);
        snprintf(label, sizeof(label), "dmacopy %zu bytes", lengths[i]);
        compare(label, rdram, &ctx, generated_dmacopy_doubleword,
                dkr_wide_dmacopy_doubleword, target, lengths[i], 0);
    }
    printf("  dmacopy         %zu lengths\n",
           sizeof(lengths) / sizeof(lengths[0]));
}

int main(void) {
    uint8_t* rdram = calloc(RDRAM_SIZE, 1);
    if (rdram == NULL) {
        fputs("out of memory\n", stderr);
        return 1;
    }

    printf("gpr width %u bytes\n", (unsigned)sizeof(gpr));
    prepare(rdram);
    exercise_atan2s(rdram);
    exercise_atan2s_domain_limit(rdram);
    exercise_rand_range(rdram);
    exercise_dmacopy(rdram);

    printf("transcript %016llx\n", transcript);
    if (sizeof(gpr) > 4) {
        printf("generated against native: %s\n",
               divergences == 0 ? "agree on every input"
                                : "DIVERGED, see above");
    } else {
        puts("generated against native: not compared, the generated bodies");
        puts("  are broken at this width and that is the whole point");
    }

    free(rdram);
    return divergences == 0 ? 0 : 1;
}
