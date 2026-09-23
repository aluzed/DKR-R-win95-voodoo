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
static u32 g_loop;   /* SETLOOP's address, already resolved */

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

static void cmd_setloop(u32 w0, u32 w1)
{
    (void)w0;
    g_loop = resolve(w1);
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

static void cmd_adpcm(u32 w0, u32 w1)     { (void)w0; (void)w1; }
static void cmd_envmixer(u32 w0, u32 w1)  { (void)w0; (void)w1; }
static void cmd_resample(u32 w0, u32 w1)  { (void)w0; (void)w1; }
static void cmd_polef(u32 w0, u32 w1)     { (void)w0; (void)w1; }

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
    g_loop = 0;
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
