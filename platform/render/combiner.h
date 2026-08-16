/* E05-S03 — from the RDP's programmable combiner to Glide's fixed one.
 *
 * Per cycle and for each term, the RDP computes `(a - b) * c + d`, choosing
 * `a`, `b`, `c`, `d` among sixteen sources. Glide offers a closed list of
 * functions and factors. Translating one into the other in general is hopeless;
 * translating the configurations DKR actually uses is a finite problem, and the
 * inventory hands them over: **33 distinct configurations**, collected not by
 * instrumentation but from the game's static tables.
 *
 * ## The structural match, and the wall
 *
 * Glide has `GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL`, which
 * computes `f x (other - local) + local`. That is **exactly** `(a - b) * c + d`
 * as soon as `d == b`. Almost every DKR configuration satisfies that equality —
 * this is not luck, it is that both pieces of hardware express the same intent:
 * interpolate between two colours.
 *
 * The wall is elsewhere, and it is sharp: **the RDP has two constant-colour
 * registers, `PRIMITIVE` and `ENVIRONMENT`; Glide has only one**
 * (`grConstantColorValue`). Any configuration that reads both at once is out of
 * reach in a single pass, however ingenious the setup. It is that criterion,
 * not case-by-case inspection, that files a configuration under `MULTIPASS` or
 * `APPROXIMATE`.
 *
 * The second wall, more expected: `TEXEL1`. Three configurations read two
 * texels, and E05-S04 is what will handle them on the second TMU.
 *
 * ## Why a data table and not a cascade of conditions
 *
 * The ticket requires it, and it is right beyond readability: a table indexed
 * by the canonical form from E04-S06 can be **walked**. We can check that no key
 * is duplicated, that every configuration in the inventory is present, and above
 * all measure each one against the reference rasteriser without writing one test
 * per case. A cascade of `if` cannot be walked.
 *
 * ## This module also holds the oracle
 *
 * The ticket assumes the E04-S08 rasteriser "implements the combiner
 * faithfully". **It did not**: it only had four fixed modes, and without a
 * faithful evaluation of `(a - b) * c + d` the criterion for measuring the gap
 * means nothing. `dkr_combiner_eval` fills that hole, and it is what produces
 * the answer Glide is compared against.
 */
#ifndef DKR_RENDER_COMBINER_H
#define DKR_RENDER_COMBINER_H

#include "rdp_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- The oracle: faithful evaluation ---------------------------------------- *
 *
 * All colours are 0..255 per component, which is the rasteriser's scale. The
 * computation is done in floating point and only clamped at the end: the RDP
 * clamps the same way, and clamping at every stage would give a different result
 * on two-cycle configurations. */
typedef struct {
    float texel0[4];       /* r, g, b, a */
    float texel1[4];
    float primitive[4];
    float shade[4];
    float environment[4];
    float combined[4];     /* result of the previous cycle; zero at cycle 0 */
    float lod_fraction;
    float prim_lod_frac;
    float k5;
} dkr_combiner_inputs;

/* Evaluates one cycle. `cycle` is 0 or 1. Writes `out[4]` in 0..255, clamped.
 *
 * **The inputs are not named the same way depending on their position.** The
 * value 6 means `1` in position `a`, `CENTER` in `b`, `SCALE` in `c` and `1` in
 * `d`; the value 1 means `TEXEL0` everywhere in colour but `TEXEL0_ALPHA` in
 * alpha. This module therefore reads each position with its own table, like the
 * E04-S06 decoder. A single dictionary would render wrong in a subtle and
 * localised way. */
void dkr_combiner_eval(const dkr_combiner *c, int cycle,
                       const dkr_combiner_inputs *in, float out[4]);

/* Evaluates the whole configuration: one or two cycles depending on
   `cycle_type`, feeding the first result back in as the second's `COMBINED`. */
void dkr_combiner_eval_all(const dkr_combiner *c, dkr_cycle_type cycle_type,
                           const dkr_combiner_inputs *in, float out[4]);

/* --- The translation to Glide ------------------------------------------------ */

typedef enum {
    DKR_CC_EXACT = 0,     /* one Glide setup produces the same result */
    DKR_CC_MULTIPASS,     /* several passes get there */
    DKR_CC_APPROXIMATE,   /* no combination gets there */
    DKR_CC_TWO_TEXELS     /* deferred to E05-S04: second TMU */
} dkr_cc_category;

const char *dkr_cc_category_text(dkr_cc_category c);

/* Which constant colour to load into Glide's single register.
 *
 * This is where the wall shows: the RDP has two, Glide only one. A configuration
 * that needs both carries `DKR_CONST_BOTH` and cannot be exact. */
typedef enum {
    DKR_CONST_NONE = 0,
    DKR_CONST_PRIMITIVE,
    DKR_CONST_ENVIRONMENT,
    DKR_CONST_BOTH
} dkr_cc_constant;

/* The Glide setup, as data. The values are those of the Glide 2.x enumerations,
   as `glide_backend.c` uses them. */
typedef struct dkr_cc_setup {
    unsigned char cc_function, cc_factor, cc_local, cc_other;
    unsigned char ac_function, ac_factor, ac_local, ac_other;
    unsigned char tc_function, tc_factor;   /* texture stage */
    unsigned char uses_texture;
} dkr_cc_setup;

typedef struct {
    const char        *name;                /* "G_CC_MODULATEIA", and so on */
    const char        *name_cycle2;         /* NULL in one-cycle mode */
    dkr_cc_stage       rgb[2], alpha[2];
    dkr_cycle_type     cycle;
    dkr_cc_category    category;
    dkr_cc_constant    constant;
    dkr_cc_setup       setup;
    const char        *note;                /* why this category */
    int                entries;             /* weight in the inventory */
} dkr_cc_entry;

int  dkr_cc_table_count(void);
const dkr_cc_entry *dkr_cc_table_at(int index);

/* Looks up by canonical form. Returns NULL if unknown — a case the caller must
   log and then render through a non-absurd fallback, as the ticket requires. */
const dkr_cc_entry *dkr_cc_lookup(unsigned long long key);

/* The canonical key of a table entry, for indexing and for the tests. */
unsigned long long dkr_cc_entry_key(const dkr_cc_entry *e);

/* The fallback, used when the configuration is unknown.
 *
 * **Visible but not absurd**, which rules out the two opposite reflexes: drawing
 * nothing, which makes a piece of scenery vanish without leaving a trace, and
 * drawing in bright magenta, which makes the game unplayable at the first
 * forgotten combiner. The fallback modulates the texture by the vertex colour —
 * the most frequent behaviour in the inventory, hence the least often wrong. */
const dkr_cc_setup *dkr_cc_fallback(void);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_COMBINER_H */
