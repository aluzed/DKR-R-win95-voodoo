/* Does a 32-bit guest register change what DKR's recompiled code computes?
 *
 * Built twice against the same source: once with the upstream recomp.h
 * (gpr = uint64_t) and once with the narrowed header that make_narrow_header.py
 * derives from it.  Each build prints a transcript; the two transcripts must
 * agree everywhere except where this file says otherwise.
 *
 * The transcript covers the four things narrowing could plausibly break:
 *   - address formation, which upstream subtracts a 64-bit constant for;
 *   - sign-dependent comparison, the classic truncation hazard;
 *   - the sd/ld pair a MIPS prologue uses to save the return address;
 *   - a doubleword block copy, which is the one place DKR genuinely uses a
 *     general register as 64 bits of data.
 *
 * The block copy appears twice: once written the way the recompiler emits it,
 * which is expected to DIVERGE under narrowing, and once with the wide locals
 * a width-aware recompiler would emit, which is expected to agree.  A test
 * where every case passes would not have shown that the first case is real.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "recomp.h"
#include "wide_register_paths.h"

#define GUEST_BASE 0x80100000u
#define RDRAM_SIZE (8u * 1024u * 1024u)

static unsigned long long transcript = 1469598103934665603ull;

/* Printed but deliberately kept out of the transcript: the one case the two
   builds are expected to disagree about. */
static void note(const char* label, unsigned long long value) {
    printf("  %-28s %016llx    (not compared)\n", label, value);
}

static void record(const char* label, unsigned long long value) {
    /* FNV-1a over the printed form, so the hash moves if the label moves. */
    const unsigned char* p = (const unsigned char*)label;
    while (*p) {
        transcript ^= *p++;
        transcript *= 1099511628211ull;
    }
    for (int i = 0; i < 8; i++) {
        transcript ^= (unsigned char)(value >> (i * 8));
        transcript *= 1099511628211ull;
    }
    printf("  %-28s %016llx\n", label, value);
}

/* A guest address as the recompiled code holds it: sign-extended when the
   register is 64 bits, plain when it is 32.  Both are the same MIPS value. */
static gpr guest(unsigned int address) {
    return (gpr)(int32_t)address;
}

static void exercise_addressing(uint8_t* rdram) {
    puts("addressing");
    gpr base = guest(GUEST_BASE);

    MEM_W(0, base) = (int32_t)0xDEADBEEF;
    MEM_W(4, base) = (int32_t)0x01020304;
    MEM_W(-4, base) = (int32_t)0x7F7F7F7F;
    record("word at +0", (uint32_t)MEM_W(0, base));
    record("word at +4", (uint32_t)MEM_W(4, base));
    record("word at -4", (uint32_t)MEM_W(-4, base));

    record("halfword sign-extended", (uint64_t)(int64_t)MEM_H(0, base));
    record("halfword zero-extended", (uint64_t)MEM_HU(0, base));
    record("byte sign-extended", (uint64_t)(int64_t)MEM_B(0, base));
    record("byte zero-extended", (uint64_t)MEM_BU(0, base));

    /* The misaligned helpers, which form addresses of their own. */
    gpr misaligned = guest(GUEST_BASE + 1);
    record("lwl", (uint32_t)do_lwl(rdram, 0, 0, misaligned));
    record("lwr", (uint32_t)do_lwr(rdram, 0, 0, misaligned));
    do_swl(rdram, 0, misaligned, guest(0x11223344u));
    record("after swl", (uint32_t)MEM_W(0, base));
    do_swr(rdram, 0, misaligned, guest(0x55667788u));
    record("after swr", (uint32_t)MEM_W(0, base));
}

/* The rewrite's algebraic claim, checked without dereferencing anything: the
   upstream 64-bit expression and the narrowed 32-bit one agree on the low word
   for every address, not merely for the ones DKR happens to use.  Both forms
   are written out here, so this runs identically in either build. */
static void exercise_offset_algebra(void) {
    static const unsigned int addresses[] = {
        0x80000000u, 0x80100000u, 0x807FFFFFu, 0x8012345Cu,
        0xA0100000u, 0xA0000000u, 0xFFFFFFFCu, 0x00001000u,
        0x7FFFFFFFu, 0x00000000u,
    };
    puts("offset algebra");
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        const unsigned int address = addresses[i];
        const uint64_t wide =
            (uint64_t)(int64_t)(int32_t)address - 0xFFFFFFFF80000000ull;
        const uint32_t narrow = (uint32_t)address - 0x80000000u;
        char label[40];
        snprintf(label, sizeof(label), "%08x agrees", address);
        record(label, (uint64_t)((uint32_t)wide == narrow));
    }
}

static void exercise_comparison(void) {
    puts("comparison");
    gpr negative = guest(0xFFFFFFFFu);
    gpr one = guest(1);
    gpr high = guest(0x80000000u);
    gpr low = guest(0x7FFFFFFFu);

    record("signed -1 < 1", (uint64_t)(SIGNED(negative) < SIGNED(one)));
    record("unsigned -1 > 1", (uint64_t)(negative > one));
    record("signed 0x80000000 < 0x7FFFFFFF",
           (uint64_t)(SIGNED(high) < SIGNED(low)));
    record("unsigned 0x80000000 > 0x7FFFFFFF", (uint64_t)(high > low));
    record("add32 wraps", (uint64_t)(uint32_t)ADD32(low, one));
    record("sub32 wraps", (uint64_t)(uint32_t)SUB32(high, one));
}

static void exercise_return_address(uint8_t* rdram) {
    puts("return address save and restore");
    gpr stack = guest(GUEST_BASE + 0x1000u);
    gpr ra = guest(0x800B6EDCu);

    SD(ra, 0, stack);
    record("stack low word", (uint32_t)MEM_W(4, stack));
    record("stack high word", (uint32_t)MEM_W(0, stack));

    gpr restored = (gpr)LD(stack, 0);
    record("restored", (uint32_t)restored);
    record("restored matches", (uint64_t)(restored == ra));
}

static unsigned long long checksum(const uint8_t* bytes, size_t count) {
    unsigned long long hash = 1469598103934665603ull;
    for (size_t i = 0; i < count; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

/* The three functions the census found, each run both ways.  The "as emitted"
   line is printed and not compared: at the upstream width it agrees with the
   wide one, and at the narrowed width it is expected not to. */
static void exercise_wide_functions(uint8_t* rdram) {
    puts("rand_range");
    static const uint32_t seeds[] = {
        0x00000001u, 0x12345678u, 0x80000000u, 0xFFFFFFFFu, 0x7A3B91C4u,
    };
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        const gpr seed = guest(seeds[i]);
        char label[48];
        snprintf(label, sizeof(label), "%08x as emitted", seeds[i]);
        note(label, (uint32_t)dkr_rand_scramble_as_emitted(seed));
        snprintf(label, sizeof(label), "%08x wide", seeds[i]);
        record(label, (uint32_t)dkr_rand_scramble_wide(seed));
    }

    puts("atan2s");
    static const uint32_t pairs[][2] = {
        { 100u, 40u }, { 0x0000FFFFu, 3u }, { 0x00100000u, 7u },
        { 1u, 1u }, { 0x7FFFFFFFu, 0x40000000u },
        /* The shift is by eleven, so a narrowed register keeps the right
           answer while the numerator fits in twenty-one bits and loses it one
           step later.  These two straddle that edge. */
        { 2097151u, 3u }, { 2097152u, 3u },
    };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        uint64_t quotient = 0, remainder = 0;
        char label[48];
        dkr_atan2s_quotient_as_emitted(guest(pairs[i][0]), guest(pairs[i][1]),
                                       &quotient, &remainder);
        snprintf(label, sizeof(label), "%u/%u as emitted index",
                 pairs[i][0], pairs[i][1]);
        note(label, quotient & 0xFFEu);
        dkr_atan2s_quotient_wide(guest(pairs[i][0]), guest(pairs[i][1]),
                                 &quotient, &remainder);
        snprintf(label, sizeof(label), "%u/%u wide index",
                 pairs[i][0], pairs[i][1]);
        record(label, quotient & 0xFFEu);
    }

    puts("dmacopy_doubleword");
    const unsigned int source_address = GUEST_BASE + 0x2000u;
    const unsigned int target_address = GUEST_BASE + 0x3000u;
    const size_t bytes = 64;

    /* A pattern whose every doubleword has a high word that is not the sign
       extension of its low word, which is what a truncating copy would lose. */
    for (size_t i = 0; i < bytes; i++) {
        rdram[(source_address - 0x80000000u) + i] = (uint8_t)(0xA0 + i);
    }

    memset(rdram + (target_address - 0x80000000u), 0, bytes);
    dkr_dmacopy_as_emitted(rdram, guest(source_address), guest(target_address),
                           guest(target_address + bytes));
    note("as emitted, matches source",
         (uint64_t)(memcmp(rdram + (source_address - 0x80000000u),
                           rdram + (target_address - 0x80000000u),
                           bytes) == 0));

    memset(rdram + (target_address - 0x80000000u), 0, bytes);
    dkr_dmacopy_wide(rdram, guest(source_address), guest(target_address),
                     guest(target_address + bytes));
    record("wide, copied bytes",
           checksum(rdram + (target_address - 0x80000000u), bytes));
    record("wide, matches source",
           (uint64_t)(memcmp(rdram + (source_address - 0x80000000u),
                             rdram + (target_address - 0x80000000u),
                             bytes) == 0));
}

int main(void) {
    uint8_t* rdram = calloc(RDRAM_SIZE, 1);
    if (rdram == NULL) {
        fputs("out of memory\n", stderr);
        return 1;
    }

    printf("gpr width %u bytes\n\n", (unsigned)sizeof(gpr));
    exercise_addressing(rdram);
    exercise_offset_algebra();
    exercise_comparison();
    exercise_return_address(rdram);
    exercise_wide_functions(rdram);
    printf("\ntranscript %016llx\n", transcript);
    puts("the two builds agree when this line matches; the \"as emitted\" lines");
    puts("are excluded, because those are the ones expected to differ");

    free(rdram);
    return 0;
}
