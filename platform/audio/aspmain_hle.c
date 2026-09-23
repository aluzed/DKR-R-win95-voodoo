/* E03-S03 - DKR's audio microcode, interpreted at a high level.
 * See aspmain_hle.h for the memory layout and the reasoning. */
#include "aspmain_hle.h"

#include <string.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef short          s16;
typedef unsigned int   u32;
typedef int            s32;

/* --- The microcode's fixed DMEM addresses ------------------------------------ */
#define DMEM_SEGMENTS   0x320u  /* sixteen words, cleared at the start of a task */
#define DMEM_STATE      0x360u  /* the mixer's state, below */
#define DMEM_CODEBOOK   0x4C0u  /* LOADADPCM's destination */
#define DMEM_BUFFERS    0x5C0u  /* every buffer address in a command is relative to it */
#define DMEM_TASK       0xFC0u

/* The state block at 0x360, halfword offsets as the microcode writes them. */
#define ST_IN        0x00u
#define ST_OUT       0x02u
#define ST_COUNT     0x04u
#define ST_VOL_L     0x06u
#define ST_VOL_R     0x08u
#define ST_AUX_DRY_R 0x0Au
#define ST_AUX_WET_L 0x0Cu
#define ST_AUX_WET_R 0x0Eu
#define ST_TARGET_L  0x10u
#define ST_RATE_L_HI 0x12u
#define ST_RATE_L_LO 0x14u
#define ST_TARGET_R  0x16u
#define ST_RATE_R_HI 0x18u
#define ST_RATE_R_LO 0x1Au
#define ST_DRY       0x1Cu
#define ST_WET       0x1Eu

/* --- Memory, in the runtime's layout ------------------------------------------ */

static u8 *g_rdram;
static u8 *g_dmem;

static u8  dmem_u8(u32 a)            { return g_dmem[(a & 0xFFFu) ^ 3u]; }
static void dmem_set_u8(u32 a, u8 v) { g_dmem[(a & 0xFFFu) ^ 3u] = v; }
static s16 dmem_s16(u32 a)           { return (s16)((dmem_u8(a) << 8) | dmem_u8(a + 1u)); }
static u16 dmem_u16(u32 a)           { return (u16)dmem_s16(a); }
static void dmem_set_s16(u32 a, s32 v)
{
    dmem_set_u8(a, (u8)((u32)v >> 8));
    dmem_set_u8(a + 1u, (u8)v);
}
static u32 dmem_u32(u32 a)
{
    return ((u32)dmem_u16(a) << 16) | dmem_u16(a + 2u);
}

static u8  rdram_u8(u32 a)            { return g_rdram[(a & 0xFFFFFFu) ^ 3u]; }
static void rdram_set_u8(u32 a, u8 v) { g_rdram[(a & 0xFFFFFFu) ^ 3u] = v; }

static u16 state_u16(u32 field)            { return dmem_u16(DMEM_STATE + field); }
static void state_set(u32 field, u32 v)    { dmem_set_s16(DMEM_STATE + field, (s32)v); }

static s32 clamp16(s32 v)
{
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
}

/* A 24-bit address through the segment table, as every command that names
 * RDRAM resolves it. */
static u32 resolve(u32 w1)
{
    const u32 segment = (w1 >> 24) & 0xFu;
    return (dmem_u32(DMEM_SEGMENTS + segment * 4u) + (w1 & 0xFFFFFFu)) & 0xFFFFFFu;
}

/* DMA as librecomp performs it, which is what the microcode's oracle and the
 * target both run: the RDRAM address loses its low three bits, the DMEM address
 * does not, and exactly `length` bytes move -- the microcode programs length - 1
 * and librecomp adds the one back. (A real RSP would round the length up to a
 * doubleword; librecomp does not, and matching librecomp is what keeps this
 * interchangeable with the recompiled microcode.) */
static void dma_read(u32 dmem_addr, u32 dram_addr, u32 length)
{
    u32 i;
    dram_addr &= 0xFFFFF8u;
    for (i = 0; i < length; i++) {
        dmem_set_u8(dmem_addr + i, rdram_u8(dram_addr + i));
    }
}

static void dma_write(u32 dmem_addr, u32 dram_addr, u32 length)
{
    u32 i;
    dram_addr &= 0xFFFFF8u;
    for (i = 0; i < length; i++) {
        rdram_set_u8(dram_addr + i, dmem_u8(dmem_addr + i));
    }
}

/* --- The simple commands -------------------------------------------------------- */

static void cmd_clearbuff(u32 w0, u32 w1)
{
    const u32 count = ((w1 & 0xFFFFu) + 15u) & ~15u;
    const u32 base = DMEM_BUFFERS + (w0 & 0xFFFFu);
    u32 i;
    for (i = 0; i < count; i++) { dmem_set_u8(base + i, 0); }
}

static void cmd_loadbuff(u32 w0, u32 w1)
{
    (void)w0;
    if (state_u16(ST_COUNT) != 0) {
        dma_read(state_u16(ST_IN), resolve(w1), state_u16(ST_COUNT));
    }
}

static void cmd_savebuff(u32 w0, u32 w1)
{
    (void)w0;
    if (state_u16(ST_COUNT) != 0) {
        dma_write(state_u16(ST_OUT), resolve(w1), state_u16(ST_COUNT));
    }
}

static void cmd_segment(u32 w0, u32 w1)
{
    const u32 segment = (w1 >> 24) & 0xFu;
    (void)w0;
    dmem_set_s16(DMEM_SEGMENTS + segment * 4u, (s32)((w1 & 0xFFFFFFu) >> 16));
    dmem_set_s16(DMEM_SEGMENTS + segment * 4u + 2u, (s32)(w1 & 0xFFFFu));
}

static void cmd_setbuff(u32 w0, u32 w1)
{
    const u32 in = DMEM_BUFFERS + (w0 & 0xFFFFu);
    const u32 out = DMEM_BUFFERS + (w1 >> 16);
    if ((w0 >> 16) & 0x08u) {                       /* A_AUX */
        state_set(ST_AUX_DRY_R, in);
        state_set(ST_AUX_WET_L, out);
        state_set(ST_AUX_WET_R, DMEM_BUFFERS + (w1 & 0xFFFFu));
    } else {
        state_set(ST_IN, in);
        state_set(ST_OUT, out);
        state_set(ST_COUNT, w1 & 0xFFFFu);
    }
}

static void cmd_setvol(u32 w0, u32 w1)
{
    const u32 flags = (w0 >> 16) & 0xFFu;
    if (flags & 0x08u) {                            /* A_AUX: dry and wet */
        state_set(ST_DRY, w0);
        state_set(ST_WET, w1);
    } else if (flags & 0x04u) {                     /* A_VOL: the current volume */
        state_set((flags & 0x02u) ? ST_VOL_L : ST_VOL_R, w0);
    } else if (flags & 0x02u) {                     /* A_LEFT: target and rate */
        state_set(ST_TARGET_L, w0);
        state_set(ST_RATE_L_HI, w1 >> 16);
        state_set(ST_RATE_L_LO, w1);
    } else {
        state_set(ST_TARGET_R, w0);
        state_set(ST_RATE_R_HI, w1 >> 16);
        state_set(ST_RATE_R_LO, w1);
    }
}

static void cmd_dmemmove(u32 w0, u32 w1)
{
    const u32 count = w1 & 0xFFFFu;
    const u32 in = DMEM_BUFFERS + (w0 & 0xFFFFu);
    const u32 out = DMEM_BUFFERS + (w1 >> 16);
    u32 i;
    /* Sixteen bytes at a time, forward, as two doubleword loads and stores:
       an overlapping move behaves as the microcode's does. */
    for (i = 0; i < ((count + 15u) & ~15u); i += 16u) {
        u8 chunk[16];
        u32 k;
        for (k = 0; k < 16u; k++) { chunk[k] = dmem_u8(((in + i) & ~7u) + k); }
        for (k = 0; k < 16u; k++) { dmem_set_u8(((out + i) & ~7u) + k, chunk[k]); }
    }
}

static void cmd_loadadpcm(u32 w0, u32 w1)
{
    dma_read(DMEM_CODEBOOK, resolve(w1), w0 & 0xFFFFu);
}

/* SETLOOP stores its resolved address as a word at state + 0x10 -- on top of
 * SETVOL's left target and rate, which ABI 1 overlaps with it. Kept overlapped:
 * a list that sets a loop and then a left volume ramp sees exactly what the
 * microcode would. */
#define ST_LOOP 0x10u
static void cmd_setloop(u32 w0, u32 w1)
{
    const u32 address = resolve(w1);
    (void)w0;
    state_set(ST_LOOP, address >> 16);
    state_set(ST_LOOP + 2u, address);
}

static void cmd_interleave(u32 w0, u32 w1)
{
    const u32 count = state_u16(ST_COUNT);
    u32 out = state_u16(ST_OUT);
    u32 left = DMEM_BUFFERS + (w1 >> 16);
    u32 right = DMEM_BUFFERS + (w1 & 0xFFFFu);
    u32 i;
    (void)w0;
    for (i = 0; i < ((count + 15u) & ~15u); i += 2u) {
        dmem_set_s16(out, dmem_s16(left + i));
        dmem_set_s16(out + 2u, dmem_s16(right + i));
        out += 4u;
    }
}

/* MIXER: out = sat16((out * K * 2 + 0x8000 + in * gain * 2) >> 16), where K is
 * element 6 of the vector the microcode loads from DMEM 0 -- VMULF of the output
 * by it, then VMACF of the input by the gain. Thirty-two bytes at a time. */
static void cmd_mixer(u32 w0, u32 w1)
{
    const u32 count = state_u16(ST_COUNT);
    const s32 gain = (s16)(w0 & 0xFFFFu);
    const s32 k = dmem_s16(0x0Cu);
    const u32 in = DMEM_BUFFERS + (w1 >> 16);
    const u32 out = DMEM_BUFFERS + (w1 & 0xFFFFu);
    u32 i;
    for (i = 0; i < ((count + 31u) & ~31u); i += 2u) {
        const long long acc = (long long)dmem_s16(out + i) * k * 2 + 0x8000 +
                              (long long)dmem_s16(in + i) * gain * 2;
        dmem_set_s16(out + i, clamp16((s32)(acc >> 16)));
    }
}

/* --- The four that do arithmetic: not yet implemented ------------------------- */

/* ADPCM: 9-byte frames, a header and sixteen 4-bit residuals, into sixteen
 * samples. The header's low nibble picks a 32-byte predictor from the codebook
 * at 0x4C0 (book0, book1), its high nibble the scale. For each half of eight:
 *
 *   S = book0[i] * l2 + book1[i] * l1 + sum_{k<i} book1[i-1-k] * in[k]
 *       + in[i] * 2048
 *   out[i] = sat16(S >> 11)            (VSAR, then * 32 and >> 16)
 *
 * where l2, l1 are the last two samples out, and in[k] the residual sign-
 * extended and shifted left by the scale, capped at 12 -- the microcode puts it
 * at the top of a halfword and shifts right by 12 - scale only when that is
 * positive. The state, the last sixteen samples, is read from the command's
 * address (or the loop's, A_LOOP) unless A_INIT, and written back there. */
/* One frame's inputs, read the way the microcode reads them: everything for
 * frame f + 1 -- header, the eight bytes of residuals, the predictor -- is loaded
 * before frame f is stored. When a frame's inputs overlap the output (a
 * predictor index past the loaded codebook reaches into the buffers), the
 * result then matches the microcode's rather than depending on write order. */
typedef struct {
    s32 shift;
    u8  data[8];
    s16 book0[8], book1[8];
} adpcm_frame;

static void adpcm_fetch(adpcm_frame *f, u32 in)
{
    const u32 header = dmem_u8(in);
    const u32 book = DMEM_CODEBOOK + (header & 0xFu) * 32u;
    u32 i;
    f->shift = 12 - (s32)(header >> 4);
    for (i = 0; i < 8u; i++) {
        f->data[i] = dmem_u8(in + 1u + i);
        f->book0[i] = dmem_s16(book + i * 2u);
        f->book1[i] = dmem_s16(book + 16u + i * 2u);
    }
}

static void cmd_adpcm(u32 w0, u32 w1)
{
    const u32 flags = (w0 >> 16) & 0xFFu;
    const u32 address = resolve(w1);
    const s32 k_scale = dmem_s16(0x08u);   /* v31[4], 32 */
    const s32 k_input = dmem_s16(0x0Au);   /* v31[5], 2048 */
    u32 in = state_u16(ST_IN);
    u32 out = state_u16(ST_OUT);
    s32 count = (s32)state_u16(ST_COUNT);
    s32 l2, l1;
    adpcm_frame frame;
    u32 i;

    for (i = 0; i < 32u; i++) { dmem_set_u8(out + i, 0); }
    if (!(flags & 0x01u)) {                              /* not A_INIT */
        const u32 from = (flags & 0x02u)                 /* A_LOOP */
            ? (((u32)state_u16(ST_LOOP) << 16) | state_u16(ST_LOOP + 2u)) : address;
        dma_read(out, from, 32u);
    }
    l2 = dmem_s16(out + 28u);
    l1 = dmem_s16(out + 30u);
    out += 32u;
    adpcm_fetch(&frame, in);

    while (count > 0) {
        s16 samples[16];
        s32 half;
        for (half = 0; half < 2; half++) {
            s32 residual[8];
            s32 j;
            for (j = 0; j < 8; j++) {
                const u32 byte = frame.data[half * 4 + j / 2];
                const u32 nibble = (j & 1) ? (byte & 0xFu) : (byte >> 4);
                s32 v = (s16)(nibble << 12);
                if (frame.shift > 0) { v >>= frame.shift; }
                residual[j] = v;
            }
            for (j = 0; j < 8; j++) {
                long long sum = (long long)frame.book0[j] * l2 +
                                (long long)frame.book1[j] * l1 +
                                (long long)residual[j] * k_input;
                s32 k;
                for (k = 0; k < j; k++) {
                    sum += (long long)frame.book1[j - 1 - k] * residual[k];
                }
                {
                    const s32 wrapped = (s32)(u32)(unsigned long long)sum;
                    samples[half * 8 + j] =
                        (s16)clamp16((s32)(((long long)wrapped * k_scale) >> 16));
                }
            }
            l2 = samples[half * 8 + 6];
            l1 = samples[half * 8 + 7];
        }
        in += 9u;
        adpcm_fetch(&frame, in);
        for (i = 0; i < 16u; i++) { dmem_set_s16(out + i * 2u, samples[i]); }
        out += 32u;
        count -= 32;
    }
    dma_write(out - 32u, address, 32u);
}

static void cmd_envmixer(u32 w0, u32 w1)  { (void)w0; (void)w1; }
/* VMULF: a 1.15 product, rounded and saturated. */
static s32 mulf(s32 a, s32 b)
{
    return clamp16((s32)(((long long)a * b * 2 + 0x8000) >> 16));
}

/* RESAMPLE: a four-tap interpolation through the table in the microcode's data
 * (DMEM 0xD0, 64 entries of four taps), the entry chosen by the top six bits of
 * the position's fraction. Each tap is a VMULF, and the four are summed pairwise
 * with saturating adds, as VADD does:
 *
 *   out = sat16(sat16(p0 + p1) + sat16(p2 + p3)),  pk = mulf(in[pos + k], lut[k])
 *
 * The position advances by pitch * 2 in 16.16, eight outputs at a time. The
 * state -- 32 bytes, kept by the microcode at DMEM 0xF90 -- holds the four input
 * samples before the next position, the fraction at +8, and, for flag 2, an
 * alignment remainder at +0xA and sixteen bytes of input at +0x10, which this
 * microcode revision adds to ABI 1. DKR's lists never set flag 2; the state is
 * still written in full, since it goes back to RDRAM. */
#define DMEM_RESAMPLE_STATE 0xF90u
#define DMEM_RESAMPLE_LUT   0xD0u
static void cmd_resample(u32 w0, u32 w1)
{
    const u32 flags = (w0 >> 16) & 0xFFu;
    const u32 pitch = w0 & 0xFFFFu;
    const u32 address = resolve(w1);
    const u32 in_start = state_u16(ST_IN);
    u32 in = in_start;
    u32 out = state_u16(ST_OUT);
    s32 count = (s32)state_u16(ST_COUNT);
    u32 fraction, i;

    if (flags & 0x01u) {                                 /* A_INIT */
        dmem_set_s16(DMEM_RESAMPLE_STATE + 8u, 0);
        for (i = 0; i < 8u; i++) { dmem_set_u8(DMEM_RESAMPLE_STATE + i, 0); }
    } else {
        dma_read(DMEM_RESAMPLE_STATE, address, 32u);
    }
    if (flags & 0x02u) {
        for (i = 0; i < 16u; i++) {
            dmem_set_u8(in - 16u + i, dmem_u8(DMEM_RESAMPLE_STATE + 0x10u + i));
        }
        in -= dmem_u16(DMEM_RESAMPLE_STATE + 0x0Au);
    }
    in -= 8u;
    for (i = 0; i < 8u; i++) { dmem_set_u8(in + i, dmem_u8(DMEM_RESAMPLE_STATE + i)); }

    fraction = dmem_u16(DMEM_RESAMPLE_STATE + 8u);
    while (count > 0) {
        u32 lane;
        for (lane = 0; lane < 8u; lane++) {
            const u32 lut = DMEM_RESAMPLE_LUT + ((fraction >> 10) & 0x3Fu) * 8u;
            const s32 p0 = mulf(dmem_s16(in + 0u), dmem_s16(lut + 0u));
            const s32 p1 = mulf(dmem_s16(in + 2u), dmem_s16(lut + 2u));
            const s32 p2 = mulf(dmem_s16(in + 4u), dmem_s16(lut + 4u));
            const s32 p3 = mulf(dmem_s16(in + 6u), dmem_s16(lut + 6u));
            dmem_set_s16(out + lane * 2u, clamp16(clamp16(p0 + p1) + clamp16(p2 + p3)));
            fraction += pitch * 2u;
            in += (fraction >> 16) * 2u;
            fraction &= 0xFFFFu;
        }
        out += 16u;
        count -= 16;
    }

    /* The microcode's scratch, which it leaves behind: the next eight outputs'
       input addresses at 0xFB0 and table addresses at 0xFC0 (over the task,
       which it has already read). Written because 0xFB0 is inside the buffer
       area, where a later SAVEBUFF would see it. */
    {
        u32 probe_in = in, probe_fraction = fraction, lane;
        for (lane = 0; lane < 8u; lane++) {
            dmem_set_s16(0xFB0u + lane * 2u, (s32)probe_in);
            dmem_set_s16(0xFC0u + lane * 2u,
                         (s32)(DMEM_RESAMPLE_LUT + ((probe_fraction >> 10) & 0x3Fu) * 8u));
            probe_fraction += pitch * 2u;
            probe_in += (probe_fraction >> 16) * 2u;
            probe_fraction &= 0xFFFFu;
        }
    }
    /* The state for the next task: the fraction, the four samples at the next
       position, and the alignment remainder with the sixteen bytes around it. */
    dmem_set_s16(DMEM_RESAMPLE_STATE + 8u, (s32)fraction);
    for (i = 0; i < 8u; i++) { dmem_set_u8(DMEM_RESAMPLE_STATE + i, dmem_u8(in + i)); }
    {
        u32 next = in + 8u;
        u32 remainder = (next - in_start) & 0xFu;
        next -= remainder;
        if (remainder != 0) { remainder = 16u - remainder; }
        dmem_set_s16(DMEM_RESAMPLE_STATE + 0x0Au, (s32)remainder);
        for (i = 0; i < 16u; i++) {
            dmem_set_u8(DMEM_RESAMPLE_STATE + 0x10u + i, dmem_u8(next + i));
        }
    }
    dma_write(DMEM_RESAMPLE_STATE, address, 32u);
}
/* POLEF: a two-pole filter shaped like the ADPCM predictor. The table is the
 * codebook area (loaded by LOADADPCM), book0 at 0x4C0 and book1 at 0x4D0, and
 * the input sample takes the command's gain where ADPCM has 2048:
 *
 *   S = book0[i] * l2 + book1[i] * l1 + sum_{k<i} book1s[i-1-k] * in[k]
 *       + in[i] * gain
 *   out[i] = sat16(S * 4 >> 16)
 *
 * book1s is book1 scaled by VMUDM against gain * 4 -- and written back over
 * book1 in DMEM, as the microcode does, while the l1 term keeps the unscaled
 * register copy. With A_INIT only the first four bytes of the state at 0xF90 are
 * cleared: l2 and l1 then come from whatever the previous command left at 0xF94,
 * and so they do here. Eight samples at a time, the next block read before the
 * current one is stored. */
static void cmd_polef(u32 w0, u32 w1)
{
    const u32 flags = (w0 >> 16) & 0xFFu;
    const s32 gain = (s16)(w0 & 0xFFFFu);
    const u32 gain4 = (w0 << 2) & 0xFFFFu;
    const s32 k_scale = 4;
    s32 count = (s32)state_u16(ST_COUNT);
    u32 in = state_u16(ST_IN);
    u32 out = state_u16(ST_OUT);
    u32 address;
    s16 book0[8], book1[8], book1s[8], input[8];
    s32 l2, l1;
    u32 i;

    if (count == 0) { return; }
    address = resolve(w1);
    for (i = 0; i < 4u; i++) { dmem_set_u8(DMEM_RESAMPLE_STATE + i, 0); }
    if (!(flags & 0x01u)) { dma_read(DMEM_RESAMPLE_STATE, address, 8u); }
    for (i = 0; i < 8u; i++) {
        book0[i] = dmem_s16(DMEM_CODEBOOK + i * 2u);
        book1[i] = dmem_s16(DMEM_CODEBOOK + 16u + i * 2u);
        book1s[i] = (s16)clamp16((s32)(((long long)book1[i] * (long long)gain4) >> 16));
        dmem_set_s16(DMEM_CODEBOOK + 16u + i * 2u, book1s[i]);
    }
    l2 = dmem_s16(DMEM_RESAMPLE_STATE + 4u);
    l1 = dmem_s16(DMEM_RESAMPLE_STATE + 6u);
    for (i = 0; i < 8u; i++) { input[i] = dmem_s16(in + i * 2u); }

    while (count > 0) {
        s16 result[8];
        s32 j;
        for (j = 0; j < 8; j++) {
            long long sum = (long long)book0[j] * l2 + (long long)book1[j] * l1 +
                            (long long)input[j] * gain;
            s32 k;
            for (k = 0; k < j; k++) { sum += (long long)book1s[j - 1 - k] * input[k]; }
            {
                const s32 wrapped = (s32)(u32)(unsigned long long)sum;
                result[j] = (s16)clamp16((s32)(((long long)wrapped * k_scale) >> 16));
            }
        }
        in += 16u;
        for (i = 0; i < 8u; i++) { input[i] = dmem_s16(in + i * 2u); }
        for (i = 0; i < 8u; i++) { dmem_set_s16(out + i * 2u, result[i]); }
        l2 = result[6];
        l1 = result[7];
        out += 16u;
        count -= 16;
    }
    dma_write(out - 8u, address, 8u);
}

/* --- The task ------------------------------------------------------------------ */

typedef void (*command_fn)(u32 w0, u32 w1);
static void cmd_spnoop(u32 w0, u32 w1) { (void)w0; (void)w1; }

static const command_fn kCommands[16] = {
    cmd_spnoop,   cmd_adpcm,     cmd_clearbuff, cmd_envmixer,
    cmd_loadbuff, cmd_resample,  cmd_savebuff,  cmd_segment,
    cmd_setbuff,  cmd_setvol,    cmd_dmemmove,  cmd_loadadpcm,
    cmd_mixer,    cmd_interleave, cmd_polef,    cmd_setloop
};

unsigned long dkr_aspmain_hle(unsigned char *rdram, unsigned char *dmem)
{
    u32 list, size, i;
    g_rdram = rdram;
    g_dmem = dmem;
    for (i = 0; i < 16u * 4u; i++) { dmem_set_u8(DMEM_SEGMENTS + i, 0); }
    /* The task, as the runtime copied it: data_ptr at +0x30, data_size at +0x34,
       native words -- the task is written into DMEM with memcpy, not by DMA. */
    memcpy(&list, dmem + DMEM_TASK + 0x30u, sizeof(list));
    memcpy(&size, dmem + DMEM_TASK + 0x34u, sizeof(size));
    list &= 0xFFFFFFu;
    for (i = 0; i + 8u <= size; i += 8u) {
        const u32 w0 = ((u32)rdram_u8(list + i) << 24) | ((u32)rdram_u8(list + i + 1u) << 16) |
                       ((u32)rdram_u8(list + i + 2u) << 8) | rdram_u8(list + i + 3u);
        const u32 w1 = ((u32)rdram_u8(list + i + 4u) << 24) | ((u32)rdram_u8(list + i + 5u) << 16) |
                       ((u32)rdram_u8(list + i + 6u) << 8) | rdram_u8(list + i + 7u);
        kCommands[(w0 >> 24) & 0xFu](w0, w1);
    }
    return size / 8u;
}
