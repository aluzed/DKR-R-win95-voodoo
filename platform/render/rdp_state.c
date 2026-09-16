/* E04-S06 — implementation. The contract lives in `rdp_state.h`. */
#include "rdp_state.h"

#include <string.h>

/* --- Combiner decoding ------------------------------------------------------ *
 *
 * The shifts come from the `GCCc0w0`, `GCCc1w0`, `GCCc0w1` and `GCCc1w1` macros
 * in `gbi.h`, **read rather than recited**. They are interleaved to the point
 * that no memory reproduces them correctly:
 *
 *   w0 (low 24 bits)   a0 << 20 (4)   c0 << 15 (5)   Aa0 << 12 (3)   Ac0 << 9 (3)
 *                      a1 <<  5 (4)   c1 <<  0 (5)
 *
 *   w1                 b0 << 28 (4)   b1 << 24 (4)   Aa1 << 21 (3)   Ac1 << 18 (3)
 *                      d0 << 15 (3)   Ab0 << 12 (3)  Ad0 <<  9 (3)   d1 <<  6 (3)
 *                      Ab1 << 3 (3)   Ad1 <<  0 (3)
 *
 * Note that the RGB fields `a` and `b` are four bits wide, `c` five, `d` three —
 * and that the alpha fields are three. Assuming a uniform width is the mistake
 * that decodes the simple cases right and the rest wrong.
 */
static unsigned field(unsigned word, int shift, int bits)
{
    return (word >> shift) & ((1u << bits) - 1u);
}

void dkr_rdp_decode_combine(unsigned int w0, unsigned int w1, dkr_combiner *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));

    out->rgb[0].a   = (unsigned char)field(w0, 20, 4);
    out->rgb[0].c   = (unsigned char)field(w0, 15, 5);
    out->alpha[0].a = (unsigned char)field(w0, 12, 3);
    out->alpha[0].c = (unsigned char)field(w0,  9, 3);
    out->rgb[1].a   = (unsigned char)field(w0,  5, 4);
    out->rgb[1].c   = (unsigned char)field(w0,  0, 5);

    out->rgb[0].b   = (unsigned char)field(w1, 28, 4);
    out->rgb[1].b   = (unsigned char)field(w1, 24, 4);
    out->alpha[1].a = (unsigned char)field(w1, 21, 3);
    out->alpha[1].c = (unsigned char)field(w1, 18, 3);
    out->rgb[0].d   = (unsigned char)field(w1, 15, 3);
    out->alpha[0].b = (unsigned char)field(w1, 12, 3);
    out->alpha[0].d = (unsigned char)field(w1,  9, 3);
    out->rgb[1].d   = (unsigned char)field(w1,  6, 3);
    out->alpha[1].b = (unsigned char)field(w1,  3, 3);
    out->alpha[1].d = (unsigned char)field(w1,  0, 3);
}

/* --- Decoding the other modes ----------------------------------------------- *
 *
 * Shifts from `G_MDSFT_*`, likewise read out of `gbi.h`:
 *
 *   mode_h   TEXTFILT 12 (2)   TEXTLOD 16 (1)   TEXTPERSP 19 (1)
 *            CYCLETYPE 20 (2)  TEXTDETAIL 17 (2)
 *   mode_l   ALPHACOMPARE 0 (2)  ZSRCSEL 2 (1)  RENDERMODE 3 (29)
 */
void dkr_rdp_decode_othermode(unsigned int mode_h, unsigned int mode_l,
                              dkr_rdp_state *out)
{
    unsigned filt;

    if (!out) {
        return;
    }
    out->cycle = (dkr_cycle_type)field(mode_h, 20, 2);

    /* G_TF_POINT is 0, G_TF_BILERP 2, G_TF_AVERAGE 3 — and **there is no value
       1**. Treating the field as a boolean would give bilinear filtering for
       `AVERAGE`, which is almost right and therefore hard to spot. We tell them
       apart, and `AVERAGE` lands on bilinear for want of anything better on the
       Glide side, which the translation reports. */
    filt = field(mode_h, 12, 2);
    out->filter = (filt == 0) ? DKR_FILTER_POINT : DKR_FILTER_BILINEAR;

    out->texture_lod    = (unsigned char)field(mode_h, 16, 1);
    out->texture_detail = (unsigned char)field(mode_h, 17, 2);
    out->texture_persp  = (unsigned char)field(mode_h, 19, 1);

    out->alpha_compare = (unsigned char)field(mode_l, 0, 2);
    out->z_source      = (unsigned char)field(mode_l, 2, 1);
    out->render_mode   = mode_l >> 3;

    /* The blender bits. `Z_CMP` and `Z_UPD` sit at 4 and 5 of the full word,
       hence at 1 and 2 once `RENDERMODE` is shifted. `G_RM_FOG_SHADE_A` is
       recognised by its blend source, which we do not decode here: fog is
       signalled by the `G_FOG` bit of the RSP's geometry mode, and the decoder
       is what carries it. */
    out->z_test  = (unsigned char)((mode_l >> 4) & 1u);
    out->z_write = (unsigned char)((mode_l >> 5) & 1u);
    /* `CVG_X_ALPHA` at 12 and `ALPHA_CVG_SEL` at 13, of the full word. They are
       the cutout, and they are not `alpha_compare` — see the note on the fields
       they fill. Decoded here and counted before they are acted on: whether DKR
       uses them at all is a question for a frame, not for a reading. */
    out->cvg_x_alpha   = (unsigned char)((mode_l >> 12) & 1u);
    out->alpha_cvg_sel = (unsigned char)((mode_l >> 13) & 1u);
    /* --- Fog, read out of the blender -------------------------------------- *
     *
     * It has no bit of its own. It follows from the blender's configuration on
     * the first cycle: colour source `G_BL_CLR_FOG`, factor `G_BL_A_SHADE`.
     * That is the very definition of `G_RM_FOG_SHADE_A`, **DKR's most frequent
     * render mode** — 74 occurrences in the game's source.
     *
     * The fields sit at fixed positions in the render-mode word: `m1a` two bits
     * at 30, `m1b` at 26. And here the RDP's trap shows up again: the value 3
     * means `G_BL_CLR_FOG` in position `m1a` but `G_BL_0` in position `m1b`.
     * Reading both with the same dictionary would declare fog where there is
     * none.
     *
     * **A consequence that reaches beyond this decoder**: the fog factor takes
     * up the vertex alpha. Anything that would like to store something else
     * there conflicts with it — see the note in
     * `docs/research/win95-fog.md`. */
    {
        const unsigned m1a = (mode_l >> 30) & 3u;   /* colour source */
        const unsigned m1b = (mode_l >> 26) & 3u;   /* alpha factor */
        out->fog = (unsigned char)(m1a == 3u && m1b == 2u);
    }
}

/* --- Canonical form --------------------------------------------------------- *
 *
 * A 64-bit key. The sixteen fields are laid out at fixed positions, with no
 * compression: two different configurations cannot collide, and a key can be
 * read back by hand when it has to be.
 *
 *   bits  0..3   rgb0.a      bits 32..35  rgb1.a
 *   bits  4..8   rgb0.c      bits 36..40  rgb1.c
 *   bits  9..12  rgb0.b      bits 41..44  rgb1.b
 *   bits 13..15  rgb0.d      bits 45..47  rgb1.d
 *   bits 16..18  a0.a        bits 48..50  a1.a
 *   bits 19..21  a0.b        bits 51..53  a1.b
 *   bits 22..24  a0.c        bits 54..56  a1.c
 *   bits 25..27  a0.d        bits 57..59  a1.d
 *   bits 28..29  the cycle mode
 *
 * **The cycle mode is part of it**, and that is not a detail: the same combiner
 * word in one cycle and in two does not produce the same image, the second stage
 * not being evaluated in the first case. Conflating them would give a mapping
 * table that renders the wrong image without ever complaining.
 */
/* **Zero has more than one spelling, and the key has to know it.**
 *
 * The RGB mux fields are wider than the list of inputs they select. In a 4-bit
 * `a` or `b` field every value from 8 to 15 means zero; in the 5-bit `c` field
 * every value from 16 to 31 does. The hardware treats them alike, so two words
 * differing only there describe **the same combiner** — and an unnormalised key
 * calls them different.
 *
 * That is not hypothetical. The table generated from the game's own static
 * tables writes `G_CC_MODULATEIA_PRIM` as `{1,8,3,7}`; the machine, decoding
 * what DKR actually sends, produces `{1,15,3,7}`. Same configuration, and the
 * lookup missed it — measured on 16 August 2026, 32,411 state applications, five
 * distinct keys, **not one of them found in the table**. Every surface in the
 * game was therefore drawn by the approximate fallback rather than by its exact
 * Glide setup.
 *
 * The 3-bit fields need nothing: there, only 7 means zero, and it is already the
 * single spelling. Normalising them anyway would be harmless and would suggest a
 * problem that does not exist.
 *
 * This is done in the key rather than in `dkr_rdp_decode_combine` on purpose.
 * The decoded structure keeps what the game sent, which is what a trace should
 * show; only the comparison needs the equivalence class. `dkr_combiner_eval`
 * reads the raw values and already treats the whole out-of-range span as zero,
 * so it is unaffected either way. */
static unsigned char zero_class(unsigned char v, unsigned char first_zero)
{
    return v >= first_zero ? first_zero : v;
}

unsigned long long dkr_rdp_combiner_key(const dkr_combiner *c,
                                        dkr_cycle_type cycle)
{
    unsigned long long k = 0;
    if (!c) {
        return 0;
    }
    k |= (unsigned long long)(zero_class(c->rgb[0].a,  8) & 0x0Fu) << 0;
    k |= (unsigned long long)(zero_class(c->rgb[0].c, 16) & 0x1Fu) << 4;
    k |= (unsigned long long)(zero_class(c->rgb[0].b,  8) & 0x0Fu) << 9;
    k |= (unsigned long long)(c->rgb[0].d   & 0x07u) << 13;
    k |= (unsigned long long)(c->alpha[0].a & 0x07u) << 16;
    k |= (unsigned long long)(c->alpha[0].b & 0x07u) << 19;
    k |= (unsigned long long)(c->alpha[0].c & 0x07u) << 22;
    k |= (unsigned long long)(c->alpha[0].d & 0x07u) << 25;
    k |= (unsigned long long)((unsigned)cycle & 0x03u) << 28;

    /* --- The second stage counts only when a second stage runs -------------- *
     *
     * `G_SETCOMBINE` always carries both stages; the RDP evaluates the second
     * one **only in two-cycle mode**. In one cycle its sixteen fields are
     * don't-care, exactly as the high spellings of zero are don't-care above,
     * and for the same reason the key must not read them.
     *
     * It is the same defect as the one the paragraph above records, one level
     * further out, and it survived that repair because both halves of the
     * comparison were being read from the same kind of source. `gbi.h` writes a
     * one-cycle mode as `gDPSetCombineMode(G_CC_X, G_CC_X)` -- the stage
     * **duplicated** -- while the generated table spells the unused stage
     * `{0,0,16,0}`. Two spellings of "not used", and the key called them two
     * combiners.
     *
     * Measured on the machine on 25 August 2026, over 960 display lists:
     * `combiners: catalogued=81360 unknown=12500`, and the three unknown keys
     * were `0EF9F031`, `0EF922C5` and `07FF7108` -- that is
     * `G_CC_MODULATEIA_PRIM`, `G_CC_BLENDT_ENV_ALPHA_A_TxP` and
     * `G_CC_PRIMITIVE`, all three already in the catalogue, all three matching
     * the game's first stage bit for bit. Twelve thousand five hundred surfaces
     * a run drawn by the approximate fallback with their exact Glide setup
     * sitting one comparison away.
     *
     * `dkr_combiner_eval_all` is the arbiter this agrees with: it too enters the
     * second stage only under `DKR_CYCLE_2`. A key that reads what the evaluator
     * ignores describes something the image does not depend on.
     *
     * The catalogue is checked against this: its 29 entries stay pairwise
     * distinct under the rule, so nothing is conflated to win the match. */
    if (cycle == DKR_CYCLE_2) {
        k |= (unsigned long long)(zero_class(c->rgb[1].a,  8) & 0x0Fu) << 32;
        k |= (unsigned long long)(zero_class(c->rgb[1].c, 16) & 0x1Fu) << 36;
        k |= (unsigned long long)(zero_class(c->rgb[1].b,  8) & 0x0Fu) << 41;
        k |= (unsigned long long)(c->rgb[1].d   & 0x07u) << 45;
        k |= (unsigned long long)(c->alpha[1].a & 0x07u) << 48;
        k |= (unsigned long long)(c->alpha[1].b & 0x07u) << 51;
        k |= (unsigned long long)(c->alpha[1].c & 0x07u) << 54;
        k |= (unsigned long long)(c->alpha[1].d & 0x07u) << 57;
    }
    return k;
}

/* --- The catalogued configurations ------------------------------------------ *
 *
 * There used to be an eight-entry `KNOWN` table here, hand-written from the
 * neighbouring port's inventory. It has been removed rather than repaired,
 * because it was wrong in a way that could not show up until the game ran, and
 * because a generated table already does its job better.
 *
 * **What was wrong.** Its entries were transcribed in a shorthand where `0`
 * meant zero — `G_CC_MODULATEIA` as `{1,0,4,0}`. The RDP does not spell zero
 * that way: in a 4-bit `b` field zero is 8 to 15, in a 5-bit `c` field 16 to 31,
 * in a 3-bit `d` field 7. A literal `0` there means `COMBINED`. So every entry
 * described a different combiner from the one it was named after, and none of
 * them could ever match a decoded configuration. Measured on the machine on
 * 17 August 2026: `catalogued=0` against `unknown=24286`, from a counter whose
 * whole purpose was to announce configurations it did not know.
 *
 * That is precisely the failure mode this file's own test warns about — "a
 * transcription goes wrong silently, and the test would then share the error of
 * the code it checks".
 *
 * **What replaces it.** `CC_TABLE` in `combiner_table.h`, generated by
 * `tools/win95/gen_combiner_table.py` from the definitions in the game's source,
 * and queried through `dkr_cc_lookup`. Twenty-nine entries instead of eight, in
 * the RDP's real encoding, and regenerated rather than retyped.
 */

/* --- Translation to the abstract state -------------------------------------- */

/* `0xRRGGBBAA`, as the RDP writes a colour register, to `0xAARRGGBB`, as
   `grConstantColorValue` reads one. See the call site for what the two spellings
   cost. */
unsigned int dkr_rdp_pack_argb(unsigned int rgba)
{
    return ((rgba & 0xFFu) << 24) | ((rgba >> 8) & 0x00FFFFFFu);
}

/* Does a stage read a texel, and which one? */
static int stage_reads(const dkr_cc_stage *s, unsigned char input)
{
    return s->a == input || s->b == input || s->c == input || s->d == input;
}

/* The factor the RDP's alpha mux applies to the texel's alpha, 0..255, where 255
   means none.

   **Narrow on purpose.** Only the shape `(TEXEL0 - 0) * C + 0` is read, C being a
   constant register, plus the degenerate shapes that yield the texel's alpha
   unchanged. That is what DKR's text uses and what a single byte can carry; any
   other shape keeps 255, which is what both backends did before this existed and
   is therefore no worse than the state we are leaving. Widening it means widening
   the field, and there is no measurement yet asking for that. */
static unsigned char alpha_scale_of(const dkr_rdp_state *rdp)
{
    /* In the alpha mux the code 7 is the constant zero, in every position. */
    enum { A_ZERO = 7u };
    const dkr_cc_stage *s = (rdp->cycle == DKR_CYCLE_2) ? &rdp->combiner.alpha[1]
                                                        : &rdp->combiner.alpha[0];

    /* --- A second cycle that only passes the first one's alpha through ------ *
     *
     * Taking the *last* cycle is right when that cycle computes something. It is
     * wrong when it is a passthrough, `(0 - 0) * 0 + COMBINED`, because then the
     * alpha was decided in cycle one and reading cycle two finds no shape it
     * knows and answers 255 -- "no scaling at all".
     *
     * `G_CC_MODULATEIA_PRIM + G_CC_BLEND_ENV_ALPHA2` is exactly that: cycle one
     * is `TEXEL0_ALPHA * PRIMITIVE_ALPHA`, cycle two passes it on. **This is the
     * configuration DKR draws its shadows with**, at a primitive alpha of 0x2D --
     * eighteen per cent. Losing it puts the shadow on at full strength, and on
     * the dialogue capture of 14 September that is a black blob beside Taj's
     * kart where the oracle lays down a faint smudge: 884 of the scene's 3113
     * divergent pixels, its largest single disagreement.
     *
     * The oracle never suffered it -- it evaluates the real two-cycle combiner
     * and does not consult this byte at all. Only the card reads it, which is why
     * the bug could sit in a shared function and show on one backend.
     *
     * The rule stays as narrow as the one below: a passthrough second cycle
     * defers to the first, and anything else is unchanged. */
    if (rdp->cycle == DKR_CYCLE_2 &&
        s->a == A_ZERO && s->b == A_ZERO && s->c == A_ZERO &&
        s->d == (unsigned char)DKR_CC_COMBINED) {
        s = &rdp->combiner.alpha[0];
    }

    /* --- A second cycle that scales the first one's alpha ------------------- *
     *
     * `(COMBINED - 0) * C + 0`, where cycle one already produced everything but
     * the constant. The generated `ac` setup has two operands and cycle one uses
     * both of them, so the constant has nowhere to go and the card draws without
     * it - at full strength where the RDP draws at a fraction.
     *
     * `G_CC_MODULATERGBA + G_CC_BLENDI_ENV_ALPHA_PRIM2` is exactly that: cycle
     * one is `TEXEL0_ALPHA x SHADE_ALPHA`, cycle two multiplies by
     * `PRIMITIVE_ALPHA`. Measured on the attract sequence at (226,295), where the
     * primitive's alpha is 106 of 255 and the vertex reaches the card at 255: the
     * oracle blends to (173,103,136) and the card to (239,239,239), near white.
     *
     * **Guarded against counting it twice.** If cycle one reads the same register,
     * the setup may already carry it, and the scale would be applied on top of
     * itself. Then the byte stays at 255, which is what it has always been.
     *
     * The rule is as narrow as the two above it, and for the same reason: this
     * byte is read by the card and not by the oracle, so widening it moves one
     * backend and not the other, and every widening has to be measured on the
     * card before it is believed. */
    if (s->a == (unsigned char)DKR_CC_COMBINED && s->b == A_ZERO &&
        s->d == A_ZERO && rdp->cycle == DKR_CYCLE_2) {
        const dkr_cc_stage *first = &rdp->combiner.alpha[0];
        if (s->c == (unsigned char)DKR_CC_PRIMITIVE &&
            !stage_reads(first, DKR_CC_PRIMITIVE)) {
            return (unsigned char)(rdp->prim_color & 0xFFu);
        }
        if (s->c == (unsigned char)DKR_CC_ENVIRONMENT &&
            !stage_reads(first, DKR_CC_ENVIRONMENT)) {
            return (unsigned char)(rdp->env_color & 0xFFu);
        }
    }

    if (s->a == (unsigned char)DKR_CC_TEXEL0 && s->b == A_ZERO &&
        s->d == A_ZERO) {
        if (s->c == (unsigned char)DKR_CC_PRIMITIVE) {
            return (unsigned char)(rdp->prim_color & 0xFFu);
        }
        if (s->c == (unsigned char)DKR_CC_ENVIRONMENT) {
            return (unsigned char)(rdp->env_color & 0xFFu);
        }
        if (s->c == A_ZERO) {
            return 0u;   /* the mux says nothing comes through */
        }
    }
    return 255u;
}

void dkr_rdp_to_render_state(const dkr_rdp_state *rdp, dkr_render_state *out,
                             int *exact)
{
    int uses_texel0, uses_texel1, uses_shade;
    int faithful = 1;

    if (!rdp || !out) {
        return;
    }
    memset(out, 0, sizeof(*out));

    uses_texel0 = stage_reads(&rdp->combiner.rgb[0], DKR_CC_TEXEL0) ||
                  stage_reads(&rdp->combiner.rgb[1], DKR_CC_TEXEL0);
    uses_texel1 = stage_reads(&rdp->combiner.rgb[0], DKR_CC_TEXEL1) ||
                  stage_reads(&rdp->combiner.rgb[1], DKR_CC_TEXEL1);
    uses_shade  = stage_reads(&rdp->combiner.rgb[0], DKR_CC_SHADE) ||
                  stage_reads(&rdp->combiner.rgb[1], DKR_CC_SHADE);

    /* --- Which constant register, if any, feeds this combiner --------------- *
     *
     * `PRIMITIVE` and `ENVIRONMENT` are the RDP's two constant colour registers,
     * and a configuration that reads one of them differs from the same
     * configuration reading the other **only by that value**. DKR's text is
     * exactly that: the same glyph drawn three to five times at identical
     * coordinates, each pass a different primitive colour.
     *
     * Primitive wins when both are read. That is a choice, not a rule -- the
     * interface carries one constant and the RDP has two -- and it is the right
     * way round for this game: of the catalogued configurations, those naming a
     * constant name `PRIMITIVE` more often, and the text passes are among them.
     * A configuration reading both is already flagged approximate below. */
    {
        const int uses_prim =
            stage_reads(&rdp->combiner.rgb[0], DKR_CC_PRIMITIVE) ||
            stage_reads(&rdp->combiner.rgb[1], DKR_CC_PRIMITIVE);
        const int uses_env =
            stage_reads(&rdp->combiner.rgb[0], DKR_CC_ENVIRONMENT) ||
            stage_reads(&rdp->combiner.rgb[1], DKR_CC_ENVIRONMENT);
        /* --- Repacked here, once, and not at each consumer ------------------ *
         *
         * `G_SETPRIMCOLOR` and `G_SETENVCOLOR` carry `0xRRGGBBAA`;
         * `grConstantColorValue` takes `0xAARRGGBB` on a context opened as
         * `GR_COLORFORMAT_ARGB`, and `backend.h` says of this very field that it
         * holds `0xAARRGGBB`. It held the RDP word instead, and the field's own
         * documentation was the one thing in the chain that was right.
         *
         * `apply_combine` compensated for that locally, with a comment recording
         * how the omission had once turned the screen's blue to zero. The
         * compensation was correct and it was in the wrong place: when
         * `gl_set_state` grew a second consumer -- the E05-S03 catalogue's Glide
         * setups -- that one read the field as documented and got the RDP word.
         *
         * The visible cost, measured on 25 August 2026 the moment the one-cycle
         * entries became reachable: the alpha of the constant is the RDP's **red**
         * channel under the wrong packing, so a pass the game means to be
         * invisible -- `G_CC_PRIMITIVE` with a primitive alpha of zero -- came out
         * fully opaque. DKR draws its character names in several passes at
         * identical coordinates, and one such pass painted a flat rectangle over
         * the letters: `TIMBER`, `TIPTUP`, `WIZPIG` all became a yellow box, and
         * the sky behind the intro went white the same way.
         *
         * A field whose two consumers disagree about its packing is a field with
         * no packing. Repacking at the source is what makes the header's sentence
         * true instead of aspirational. */
        if (uses_prim) {
            out->constant_color = dkr_rdp_pack_argb(rdp->prim_color);
        } else if (uses_env) {
            out->constant_color = dkr_rdp_pack_argb(rdp->env_color);
        }
        /* And both of them, unconditionally. The choice above is Glide's
           limitation -- one constant register -- and not a fact about the RDP,
           which has two and may read both in the same configuration. A rasteriser
           that evaluates the real combiner needs both. */
        out->prim_color = dkr_rdp_pack_argb(rdp->prim_color);
        out->env_color  = dkr_rdp_pack_argb(rdp->env_color);
        /* --- And what the **alpha** mux does with it ----------------------- *
         *
         * The alpha mux is separate from the colour one on the RDP, and until
         * 4 September 2026 nothing here consulted it for this mode. The two
         * backends each decided alone and decided differently: the oracle took
         * the texel's alpha untouched, the Glide path multiplied it by the alpha
         * of whichever register the *colour* side had named.
         *
         * The second is wrong twice over. The RDP need not scale the alpha at
         * all, and when it does it may name the **other** register: the
         * configuration this game's text uses,
         * `G_CC_BLENDT_ENV_ALPHA_A_TxP`, takes its colour from ENVIRONMENT and
         * its alpha from PRIMITIVE. Multiplying by the environment's alpha was
         * therefore not an approximation of anything — and the last of the five
         * text passes carries an environment alpha of zero, so the pass that
         * paints the letters was thrown away. 2416 pixels of a nameplate.
         *
         * So the factor is read from the alpha mux and carried as a value rather
         * than as a flag, which is what lets the Glide backend hand the card the
         * right number instead of the wrong register's. */
        out->alpha_scale = alpha_scale_of(rdp);
        if (uses_prim && uses_env) {
            /* Two constants, one register on our side. Announced rather than
               silently halved. */
            faithful = 0;
        }
    }

    if (uses_texel0 && uses_shade) {
        /* Does alpha come from the texel or from shading? The distinction
           decides the transparency of cut-outs, and getting it wrong gives hard
           edges where the game expects a gradient. */
        const int alpha_from_texel =
            stage_reads(&rdp->combiner.alpha[0], DKR_CC_TEXEL0) ||
            stage_reads(&rdp->combiner.alpha[1], DKR_CC_TEXEL0);
        out->combine = alpha_from_texel ? DKR_COMBINE_TEXTURE_SHADE_ALPHA
                                        : DKR_COMBINE_TEXTURE_SHADE;
    } else if (uses_texel0) {
        /* Texel with a constant is a different image from the texel alone, and
           it is the case DKR's multi-pass text lands in. Distinguishing them is
           what lets one pass differ from the next. */
        out->combine = (out->constant_color != 0u) ? DKR_COMBINE_TEXTURE_CONSTANT
                                                   : DKR_COMBINE_TEXTURE;
    } else {
        out->combine = DKR_COMBINE_SHADE;
    }

    /* **What the interface cannot express.** Two texels need two TMUs or a
       second pass (E05-S04); an arbitrary second stage has no equivalent among
       E04-S01's four modes. We return the nearest mode and **give warning**,
       because an approximate translation that does not announce itself produces
       a plausible, wrong image — the worst outcome for a port whose oracle is
       the image. */
    if (uses_texel1) {
        faithful = 0;
    }
    if (rdp->cycle == DKR_CYCLE_2) {
        faithful = 0;
    }

    out->filter = rdp->filter;
    out->depth  = rdp->z_test ? (rdp->z_write ? DKR_DEPTH_TEST_AND_WRITE
                                              : DKR_DEPTH_TEST_ONLY)
                              : DKR_DEPTH_DISABLED;
    /* --- The cutout the RDP does not call an alpha test --------------------- *
     *
     * `alpha_compare` alone was the whole of this line, and it is zero on every
     * frame this port has measured — including the ones where a palm tree is
     * drawn inside an opaque grey rectangle, which is a cutout failing if
     * anything is.
     *
     * The RDP has a second mechanism and DKR uses that one. `CVG_X_ALPHA`
     * multiplies the coverage by the alpha, so a texel at alpha zero covers
     * nothing and never reaches the frame buffer; the `G_RM_*TEX_EDGE` render
     * modes are built on it. Measured on the character-select and Ancient Lake
     * lists, 24 August 2026: `cvg-x-alpha` between 2 and 47 state applications
     * per frame, `alpha-cvg-sel` between 37 and 108, and `alpha-test` zero
     * throughout. The port was reading the one bit the game never sets.
     *
     * The threshold is 1 rather than 128 because that is what the mechanism
     * says: coverage times alpha removes only what has *no* alpha. A texture
     * with one alpha bit — RGBA16, which is what these surfaces carry — is then
     * translated exactly. Where the alpha has more bits the N64 dithers a
     * partial coverage and a hard threshold cannot; that is an approximation,
     * and it is this one rather than a rounder-looking 128 because 128 would
     * also throw away every half-transparent texel the game did mean to keep.
     *
     * `ALPHA_CVG_SEL` is counted but not acted on: on its own it feeds the
     * coverage back as the alpha, which is antialiasing and not a cutout. */
    out->alpha_test      = (unsigned char)(rdp->alpha_compare != 0 ||
                                           rdp->cvg_x_alpha != 0);
    out->alpha_reference = (unsigned char)((rdp->alpha_compare != 0) ? 128 : 1);
    out->fog_enabled     = rdp->fog;
    /* --- Blending, derived from the blender instead of assumed -------------- *
     *
     * This line used to be `DKR_BLEND_ALPHA` **unconditionally**. The blender
     * word was decoded into `rdp->render_mode` just above, and never consulted:
     * every surface in the game was therefore blended, opaque ones included,
     * which makes the image invisible as soon as an alpha is missing.
     *
     * The defect stayed hidden as long as depth was inactive — with no test,
     * each triangle covered the previous one and the last one showed. It came
     * out at the moment `G_RDPSETOTHERMODE` was wired up, which made it look
     * like a symptom of that fix. Two defects, one masking the other, and it is
     * the second one that gets blamed.
     *
     * `FORCE_BL` — bit 14 of the low word — is what distinguishes a genuinely
     * blended surface from an opaque one: the RDP requires it for the blender to
     * act on the second cycle. `G_RM_OPA_SURF` does not carry it,
     * `G_RM_XLU_SURF` and `G_RM_AA_ZB_XLU_SURF` do.
     *
     * The second cycle's `B` factor then separates the two blends this port can
     * do: `G_BL_1MA` (value 0) is classic alpha blending; the rest is reduced to
     * additive for want of anything better, and that approximation is reported
     * through `faithful`.
     *
     * **The positions account for the shift of three.** `render_mode` is the low
     * word shifted by 3 — see its assignment above — so `FORCE_BL`, which is bit
     * 14 of the full word, sits here at bit 11, and the second cycle's `B`
     * field, bits 16 and 17 of the full word, at bits 13 and 14. Writing the
     * full word's positions would have read neighbouring fields: the result
     * would have been plausible — a chosen blend, differing between surfaces —
     * and wrong, hence invisible to inspection. */
    {
        const unsigned int force_bl = rdp->render_mode & 0x800u;
        const unsigned int b2 = (rdp->render_mode >> 13) & 3u;
        if (force_bl == 0u) {
            out->blend = DKR_BLEND_OPAQUE;
        } else if (b2 == 0u) {
            out->blend = DKR_BLEND_ALPHA;
        } else {
            out->blend = DKR_BLEND_ADDITIVE;
            faithful = 0;
        }
    }
    out->cull            = DKR_CULL_NONE;   /* carried by the geometry mode */
    out->wrap_s = out->wrap_t = DKR_WRAP_REPEAT;

    if (exact) {
        *exact = faithful;
    }
}
