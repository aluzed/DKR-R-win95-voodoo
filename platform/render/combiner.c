/* E05-S03 — implementation. The contract and the analysis live in `combiner.h`. */
#include "combiner.h"
#include "combiner_table.h"

#include <string.h>

/* --- The sources, read position by position --------------------------------- *
 *
 * A single lookup table would be wrong. The value 6 means `1` in position `a`,
 * `CENTER` in `b`, `SCALE` in `c` and `1` in `d`; the value 1 means `TEXEL0` in
 * colour but `TEXEL0_ALPHA` in alpha. Each position therefore has its own
 * function, and that is deliberately verbose: the compact version of this code
 * would be the wrong version.
 *
 * `out[3]` receives a colour; scalar sources fill all four components, which
 * lets the caller multiply without wondering whether it is holding a colour or
 * a scalar. */

static void splat(float v, float out[4])
{
    out[0] = out[1] = out[2] = out[3] = v;
}

static void copy4(const float in[4], float out[4])
{
    out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; out[3] = in[3];
}

static void rgb_a(const dkr_combiner_inputs *in, unsigned v, float out[4])
{
    switch (v) {
    case 0:  copy4(in->combined, out);    break;
    case 1:  copy4(in->texel0, out);      break;
    case 2:  copy4(in->texel1, out);      break;
    case 3:  copy4(in->primitive, out);   break;
    case 4:  copy4(in->shade, out);       break;
    case 5:  copy4(in->environment, out); break;
    case 6:  splat(255.0f, out);          break;   /* 1 */
    /* 7 = NOISE. The rasteriser produces none: a random source would make the
       comparison against the card impossible, and DKR does not use it. Zero is
       the choice that shows up least if it ever did appear, and the inventory
       would say that it had. */
    default: splat(0.0f, out);            break;
    }
}

static void rgb_b(const dkr_combiner_inputs *in, unsigned v, float out[4])
{
    switch (v) {
    case 0:  copy4(in->combined, out);    break;
    case 1:  copy4(in->texel0, out);      break;
    case 2:  copy4(in->texel1, out);      break;
    case 3:  copy4(in->primitive, out);   break;
    case 4:  copy4(in->shade, out);       break;
    case 5:  copy4(in->environment, out); break;
    /* 6 = CENTER, 7 = K4: chroma-conversion registers, which DKR does not
       use. */
    default: splat(0.0f, out);            break;
    }
}

static void rgb_c(const dkr_combiner_inputs *in, unsigned v, float out[4])
{
    switch (v) {
    case 0:  copy4(in->combined, out);    break;
    case 1:  copy4(in->texel0, out);      break;
    case 2:  copy4(in->texel1, out);      break;
    case 3:  copy4(in->primitive, out);   break;
    case 4:  copy4(in->shade, out);       break;
    case 5:  copy4(in->environment, out); break;
    case 6:  splat(255.0f, out);          break;   /* SCALE, no register here */
    case 7:  splat(in->combined[3], out);    break;
    case 8:  splat(in->texel0[3], out);      break;
    case 9:  splat(in->texel1[3], out);      break;
    case 10: splat(in->primitive[3], out);   break;
    case 11: splat(in->shade[3], out);       break;
    case 12: splat(in->environment[3], out); break;
    case 13: splat(in->lod_fraction, out);   break;
    case 14: splat(in->prim_lod_frac, out);  break;
    case 15: splat(in->k5, out);             break;
    default: splat(0.0f, out);               break;
    }
}

static void rgb_d(const dkr_combiner_inputs *in, unsigned v, float out[4])
{
    switch (v) {
    case 0:  copy4(in->combined, out);    break;
    case 1:  copy4(in->texel0, out);      break;
    case 2:  copy4(in->texel1, out);      break;
    case 3:  copy4(in->primitive, out);   break;
    case 4:  copy4(in->shade, out);       break;
    case 5:  copy4(in->environment, out); break;
    case 6:  splat(255.0f, out);          break;
    default: splat(0.0f, out);            break;
    }
}

/* The alpha terms. `a`, `b` and `d` share one table; `c` has another, where the
   value 0 means `LOD_FRACTION` and not `COMBINED`. */
static float alpha_abd(const dkr_combiner_inputs *in, unsigned v)
{
    switch (v) {
    case 0:  return in->combined[3];
    case 1:  return in->texel0[3];
    case 2:  return in->texel1[3];
    case 3:  return in->primitive[3];
    case 4:  return in->shade[3];
    case 5:  return in->environment[3];
    case 6:  return 255.0f;
    default: return 0.0f;
    }
}

static float alpha_c(const dkr_combiner_inputs *in, unsigned v)
{
    switch (v) {
    case 0:  return in->lod_fraction;
    case 1:  return in->texel0[3];
    case 2:  return in->texel1[3];
    case 3:  return in->primitive[3];
    case 4:  return in->shade[3];
    case 5:  return in->environment[3];
    case 6:  return in->prim_lod_frac;
    default: return 0.0f;
    }
}

static float clamp255(float v)
{
    return (v < 0.0f) ? 0.0f : (v > 255.0f ? 255.0f : v);
}

void dkr_combiner_eval(const dkr_combiner *c, int cycle,
                       const dkr_combiner_inputs *in, float out[4])
{
    float a[4], b[4], k[4], d[4];
    const dkr_cc_stage *rs, *as;
    int i;

    if (!c || !in || !out || cycle < 0 || cycle > 1) { return; }
    rs = &c->rgb[cycle];
    as = &c->alpha[cycle];

    rgb_a(in, rs->a, a);
    rgb_b(in, rs->b, b);
    rgb_c(in, rs->c, k);
    rgb_d(in, rs->d, d);

    /* `(a - b) * c + d`, in 0..255. The factor `c` is itself in 0..255 and must
       therefore be scaled down: the RDP treats it as a fraction. */
    for (i = 0; i < 3; i++) {
        out[i] = clamp255((a[i] - b[i]) * (k[i] / 255.0f) + d[i]);
    }
    {
        const float aa = alpha_abd(in, as->a);
        const float ab = alpha_abd(in, as->b);
        const float ak = alpha_c(in, as->c);
        const float ad = alpha_abd(in, as->d);
        out[3] = clamp255((aa - ab) * (ak / 255.0f) + ad);
    }
}

void dkr_combiner_eval_all(const dkr_combiner *c, dkr_cycle_type cycle_type,
                           const dkr_combiner_inputs *in, float out[4])
{
    dkr_combiner_inputs work;

    if (!c || !in || !out) { return; }
    work = *in;
    /* On the first cycle, `COMBINED` has no value. The RDP reads the previous
       triangle's result there, which is undefined from the program's point of
       view; zero is the only value that makes rendering reproducible, and that
       is what comparison against the card demands. */
    work.combined[0] = work.combined[1] = work.combined[2] = work.combined[3] = 0.0f;

    dkr_combiner_eval(c, 0, &work, out);
    if (cycle_type == DKR_CYCLE_2) {
        copy4(out, work.combined);
        dkr_combiner_eval(c, 1, &work, out);
    }
}

/* --- The table --------------------------------------------------------------- */

const char *dkr_cc_category_text(dkr_cc_category c)
{
    switch (c) {
    case DKR_CC_EXACT:       return "exact";
    case DKR_CC_MULTIPASS:   return "multipass";
    case DKR_CC_APPROXIMATE: return "approximate";
    default:                 return "two texels (E05-S04)";
    }
}

#define CC_COUNT ((int)(sizeof(CC_TABLE) / sizeof(CC_TABLE[0])))

int dkr_cc_table_count(void) { return CC_COUNT; }

const dkr_cc_entry *dkr_cc_table_at(int index)
{
    if (index < 0 || index >= CC_COUNT) { return 0; }
    return &CC_TABLE[index];
}

unsigned long long dkr_cc_entry_key(const dkr_cc_entry *e)
{
    dkr_combiner c;
    if (!e) { return 0; }
    memset(&c, 0, sizeof(c));
    c.rgb[0]   = e->rgb[0];   c.rgb[1]   = e->rgb[1];
    c.alpha[0] = e->alpha[0]; c.alpha[1] = e->alpha[1];
    return dkr_rdp_combiner_key(&c, e->cycle);
}

const dkr_cc_entry *dkr_cc_lookup(unsigned long long key)
{
    int i;
    /* A linear search over twenty-nine entries, called on state changes and not
       per triangle. A hash table would gain nothing measurable and would read
       less well. */
    for (i = 0; i < CC_COUNT; i++) {
        if (dkr_cc_entry_key(&CC_TABLE[i]) == key) {
            return &CC_TABLE[i];
        }
    }
    return 0;
}

const dkr_cc_setup *dkr_cc_fallback(void)
{
    /* Texture modulated by the vertex colour: `SCALE_OTHER` with the local
       colour as factor. That is the most frequent behaviour in the inventory,
       hence the one most likely to be right on a configuration we did not
       anticipate.
     *
     * The choice reads against its two alternatives, both discarded: drawing
     * nothing would make a piece of scenery vanish without leaving a trace, and
     * painting in bright magenta would make the game unplayable at the first
     * forgotten combiner. "Visible but not absurd" is what the ticket asks
     * for. */
    static const dkr_cc_setup fallback = {
        3, 1, 0, 1,      /* colour: SCALE_OTHER, factor LOCAL, local iterated, other texture */
        3, 1, 0, 1,      /* alpha: likewise */
        1, 0,            /* texture stage: DECAL */
        1
    };
    return &fallback;
}
