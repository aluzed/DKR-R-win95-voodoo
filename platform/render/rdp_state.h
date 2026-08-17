/* E04-S06 — decoding the RDP state, and translating it into the abstract state.
 *
 * The RDP is driven by a dense state, encoded in a handful of 64-bit words with
 * interleaved fields. The colour combiner deserves a mention of its own: it is a
 * programmable unit that computes, for every pixel, a combination of texel,
 * primitive colour, environment colour, shade colour and constants — over one or
 * two cycles.
 *
 * ## What makes the problem tractable
 *
 * Glide's fixed combiner is far less expressive, and the translation is the hard
 * point of the whole E05 epic. But **DKR declares its render setups in static
 * tables**: the set of combiners in use is bounded and known. The neighbouring
 * native port's inventory counts **33 distinct configurations**, of which
 * **only 3 read two texels**.
 *
 * So this is not about translating a programmable combiner in general, but about
 * matching 33 enumerated cases. That is what gives this file its shape: decoding
 * produces a **comparable canonical form**, and E05-S03 will match a Glide setup
 * to it by simple lookup.
 *
 * ## What this file does not do
 *
 * It renders nothing. Translation to Glide is E05-S03 through E05-S06; here we
 * decode and file.
 */
#ifndef DKR_RENDER_RDP_STATE_H
#define DKR_RENDER_RDP_STATE_H

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- The combiner inputs ---------------------------------------------------- *
 *
 * Values of `G_CCMUX_*` and `G_ACMUX_*` from `gbi.h`. Copied rather than
 * included: this port does not have the decompilation's headers — it works from
 * recompiled MIPS — and a handful of constants beats a dependency on a
 * neighbouring repository.
 *
 * The trap in this table is that **the same number does not designate the same
 * thing depending on position**. `G_CCMUX_CENTER` and `G_CCMUX_SCALE` are both
 * 6; `1` means `TEXEL0` in input A but `NOISE` nowhere else. The decoder
 * therefore names the inputs by position, and not through a single dictionary.
 */
typedef enum {
    DKR_CC_COMBINED = 0,
    DKR_CC_TEXEL0,
    DKR_CC_TEXEL1,
    DKR_CC_PRIMITIVE,
    DKR_CC_SHADE,
    DKR_CC_ENVIRONMENT,
    DKR_CC_CENTER_SCALE,     /* 6: `CENTER` or `SCALE` depending on position */
    DKR_CC_COMBINED_ALPHA,   /* 7 */
    DKR_CC_TEXEL0_ALPHA,
    DKR_CC_TEXEL1_ALPHA,
    DKR_CC_PRIMITIVE_ALPHA,
    DKR_CC_SHADE_ALPHA,
    DKR_CC_ENV_ALPHA,
    DKR_CC_LOD_FRACTION,
    DKR_CC_PRIM_LOD_FRAC,
    DKR_CC_K5,
    DKR_CC_ZERO_OR_OTHER     /* >= 16: zero for the 4-bit fields */
} dkr_cc_input;

/* One stage: `(a - b) * c + d`. That is the RDP's form, and keeping it as-is
   avoids losing along the way the information E05-S03 will need. */
typedef struct {
    unsigned char a, b, c, d;
} dkr_cc_stage;

typedef struct {
    dkr_cc_stage rgb[2];     /* cycle 0, cycle 1 */
    dkr_cc_stage alpha[2];
} dkr_combiner;

/* --- The cycle mode -------------------------------------------------------- */
typedef enum {
    DKR_CYCLE_1 = 0,
    DKR_CYCLE_2,
    DKR_CYCLE_COPY,
    DKR_CYCLE_FILL
} dkr_cycle_type;

/* --- The decoded state ----------------------------------------------------- */
typedef struct {
    dkr_cycle_type  cycle;
    dkr_combiner    combiner;

    /* Texture modes, from `G_SETOTHERMODE_H`. */
    dkr_filter_mode filter;          /* G_TF_POINT / G_TF_BILERP / G_TF_AVERAGE */
    unsigned char   texture_lod;     /* G_TL_LOD */
    unsigned char   texture_persp;   /* G_TP_PERSP */
    unsigned char   texture_detail;  /* G_TD_* */

    /* Render modes, from `G_SETOTHERMODE_L`. */
    unsigned char   alpha_compare;   /* G_AC_*: 0 none, 1 threshold, 2 dither */
    unsigned char   z_source;        /* G_ZS_* */
    unsigned int    render_mode;     /* the blender bits, raw */

    /* What the blender says, once read. The RDP's bits are interleaved and we
       would rather decode them once. */
    unsigned char   z_test;
    unsigned char   z_write;
    unsigned char   fog;
} dkr_rdp_state;

/* --- Decoding -------------------------------------------------------------- */

/* `G_SETCOMBINE`: two 32-bit words. `w0` carries the opcode at the front, which
   the decoder ignores — it reads only the low 24 bits. */
void dkr_rdp_decode_combine(unsigned int w0, unsigned int w1,
                            dkr_combiner *out);

/* `G_SETOTHERMODE_H` and `_L`, as the RSP maintains them. */
void dkr_rdp_decode_othermode(unsigned int mode_h, unsigned int mode_l,
                              dkr_rdp_state *out);

/* --- Canonical form -------------------------------------------------------- *
 *
 * A 64-bit key that identifies a combiner configuration. Two configurations are
 * the same if and only if their keys are equal — that is what lets E05-S03 look
 * up rather than reason.
 *
 * The cycle mode **is part of it**: the same combiner word in one cycle and in
 * two cycles does not produce the same image, the second stage not being
 * evaluated in the first case. Conflating them would be a silent error. */
unsigned long long dkr_rdp_combiner_key(const dkr_combiner *c,
                                        dkr_cycle_type cycle);

/* **The catalogue lives in `combiner.h`**, and is queried through
 * `dkr_cc_lookup`.
 *
 * The safety net from step 6 of the ticket is unchanged — a case that is not
 * catalogued must announce itself rather than render wrongly in silence, a
 * missed configuration being invisible at decode time and visible on screen —
 * but it is `CC_TABLE` that answers, generated from the game's source by
 * `tools/win95/gen_combiner_table.py`.
 *
 * `dkr_rdp_combiner_name`, `dkr_rdp_known_count` and `dkr_rdp_known_at` are
 * gone with the hand-written eight-entry table they served. It was transcribed
 * in a shorthand where `0` meant zero, which is not how the RDP spells it, so
 * none of its entries could ever match; see the note in `rdp_state.c`. */

/* --- Translation to the abstract state ------------------------------------- *
 *
 * Fills in what E04-S01 defines. Whatever has no equivalent — a combiner with
 * two arbitrary stages — is reduced to the nearest mode, and `*exact` receives 0
 * to say so. **An approximate translation that does not announce itself is worse
 * than a failure**: it produces a plausible, wrong image. */
void dkr_rdp_to_render_state(const dkr_rdp_state *rdp,
                             dkr_render_state *out, int *exact);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_RDP_STATE_H */
