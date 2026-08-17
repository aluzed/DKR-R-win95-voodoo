/* E04-S06 — the RDP state decoding test.
 *
 * The vectors come from the **decompilation's headers** — the 63 `G_CC_*` macros
 * resolved by `tools/win95/gen_combiner_vectors.py` — and not from a
 * transcription by hand. The distinction is what gives the test its value: a
 * transcription goes wrong silently, and the test would then share the error of
 * the code it checks.
 *
 * What is established here is narrow and sharp: **the decoder recovers exactly
 * what the game's encoder wrote**, for every configuration the decompilation
 * knows.
 */
#include "render/rdp_state.h"
#include "render/combiner.h"

#include <stdio.h>
#include <string.h>

#include "combiner_vectors.inc"

static int g_fails;
static FILE *g_out;

static void check(const char *what, int condition)
{
    printf("  %s %s\n", condition ? "ok   " : "FAIL ", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", condition ? "ok   " : "FAIL ", what);
        fflush(g_out);
    }
    if (!condition) { g_fails++; }
}

int main(void)
{
    const int count = (int)(sizeof(COMBINER_VECTORS) / sizeof(COMBINER_VECTORS[0]));
    int i, mismatches = 0;
    char label[96];

    g_out = fopen("D:\\RDPSTATE.TXT", "w");

    /* --- Decoding the combiner --------------------------------------------- */
    for (i = 0; i < count; i++) {
        const combiner_vector *v = &COMBINER_VECTORS[i];
        dkr_combiner c;
        int ok;
        dkr_rdp_decode_combine(v->w0, v->w1, &c);
        ok = c.rgb[0].a == v->a && c.rgb[0].b == v->b &&
             c.rgb[0].c == v->c && c.rgb[0].d == v->d &&
             c.alpha[0].a == v->Aa && c.alpha[0].b == v->Ab &&
             c.alpha[0].c == v->Ac && c.alpha[0].d == v->Ad &&
             /* `gsDPSetCombineMode(x, x)` writes the same set into both
                cycles: the second must therefore decode identically. Checking it
                catches shift errors on the cycle-1 fields, which are the easiest
                to confuse. */
             c.rgb[1].a == v->a && c.rgb[1].b == v->b &&
             c.rgb[1].c == v->c && c.rgb[1].d == v->d;
        if (!ok) {
            mismatches++;
            if (mismatches <= 5) {
                sprintf(label, "%s: expected (%u,%u,%u,%u) got (%u,%u,%u,%u)",
                        v->name, v->a, v->b, v->c, v->d,
                        c.rgb[0].a, c.rgb[0].b, c.rgb[0].c, c.rgb[0].d);
                check(label, 0);
            }
        }
    }
    sprintf(label, "the %d decompilation configurations decode", count);
    check(label, mismatches == 0);

    /* --- The canonical form ------------------------------------------------ */
    {
        dkr_combiner a, b;
        dkr_rdp_decode_combine(COMBINER_VECTORS[0].w0, COMBINER_VECTORS[0].w1, &a);
        dkr_rdp_decode_combine(COMBINER_VECTORS[1].w0, COMBINER_VECTORS[1].w1, &b);
        check("two different configurations have two different keys",
              dkr_rdp_combiner_key(&a, DKR_CYCLE_1) !=
              dkr_rdp_combiner_key(&b, DKR_CYCLE_1));
        check("the same configuration gives the same key",
              dkr_rdp_combiner_key(&a, DKR_CYCLE_1) ==
              dkr_rdp_combiner_key(&a, DKR_CYCLE_1));
        /* The point that counts: the cycle mode is part of the identity. The
           same word in one cycle and in two does not produce the same image, the
           second stage not being evaluated in the first case. */
        check("one cycle and two cycles are not the same configuration",
              dkr_rdp_combiner_key(&a, DKR_CYCLE_1) !=
              dkr_rdp_combiner_key(&a, DKR_CYCLE_2));
    }

    /* --- Zero has more than one spelling ------------------------------------ *
     *
     * The RGB mux fields are wider than the inputs they select: in a 4-bit `a`
     * or `b` every value from 8 to 15 means zero, in the 5-bit `c` every value
     * from 16 to 31 does. Two words differing only there are the **same**
     * combiner, and the key has to say so.
     *
     * This is the defect that made the whole lookup useless: the table
     * generated from the game's static tables writes `G_CC_MODULATEIA_PRIM` as
     * `{1,8,3,7}`, and the machine, decoding what DKR sends, produced
     * `{1,15,3,7}`. 32,411 state applications, five distinct keys, not one
     * found -- so every surface was drawn by the approximate fallback.
     *
     * The check is written in both directions on purpose. Only asserting that
     * the spellings agree would also pass on a key that returned a constant. */
    {
        dkr_combiner canonical, spelled, different;
        unsigned char s;

        memset(&canonical, 0, sizeof(canonical));
        canonical.rgb[0].a = 1;  canonical.rgb[0].b = 8;
        canonical.rgb[0].c = 3;  canonical.rgb[0].d = 7;
        canonical.alpha[0].a = 1; canonical.alpha[0].b = 7;
        canonical.alpha[0].c = 3; canonical.alpha[0].d = 7;

        for (s = 8; s <= 15; s++) {
            spelled = canonical;
            spelled.rgb[0].b = s;
            if (dkr_rdp_combiner_key(&spelled, DKR_CYCLE_1) !=
                dkr_rdp_combiner_key(&canonical, DKR_CYCLE_1)) {
                sprintf(label, "b=%u spells the same zero as b=8", s);
                check(label, 0);
                break;
            }
        }
        if (s > 15) {
            check("every 4-bit spelling of zero gives the same key", 1);
        }

        for (s = 16; s <= 31; s++) {
            spelled = canonical;
            spelled.rgb[0].c = s;
            if (dkr_rdp_combiner_key(&spelled, DKR_CYCLE_1) ==
                dkr_rdp_combiner_key(&canonical, DKR_CYCLE_1)) {
                break;      /* c=3 is not zero, so these must differ */
            }
        }
        check("a 5-bit zero is not confused with a non-zero c", s > 31);

        /* The negative control: normalisation must not flatten everything. An
           input that really differs still has to give a different key. */
        different = canonical;
        different.rgb[0].b = 4;            /* SHADE, not zero */
        check("a real change of input still changes the key",
              dkr_rdp_combiner_key(&different, DKR_CYCLE_1) !=
              dkr_rdp_combiner_key(&canonical, DKR_CYCLE_1));
    }

    /* Every key must be pairwise distinct: a collision would match one
       configuration to another's Glide setup, and the image would be wrong with
       nothing reporting it.
     *
     * **The comparison is between equivalence classes, not between structures.**
     * Since the key normalises the spellings of zero, two configurations
     * differing only there are the same combiner and *must* share a key -- a
     * `memcmp` would call that a collision and fail on a correct
     * implementation. So the pair is skipped when the two are equivalent, which
     * is exactly what `dkr_rdp_combiner_key` itself decides. Writing this with
     * `memcmp` was right before normalisation and became wrong with it. */
    {
        int collisions = 0, j;
        for (i = 0; i < count; i++) {
            dkr_combiner ci;
            dkr_rdp_decode_combine(COMBINER_VECTORS[i].w0, COMBINER_VECTORS[i].w1, &ci);
            for (j = i + 1; j < count; j++) {
                dkr_combiner cj;
                unsigned long long ki, kj;
                dkr_rdp_decode_combine(COMBINER_VECTORS[j].w0, COMBINER_VECTORS[j].w1, &cj);
                ki = dkr_rdp_combiner_key(&ci, DKR_CYCLE_1);
                kj = dkr_rdp_combiner_key(&cj, DKR_CYCLE_1);
                /* Equal keys are a collision only if the two really compute
                   different things. `dkr_combiner_eval` is the arbiter: it is
                   what the image depends on. */
                if (ki == kj) {
                    /* Each source gets a distinct value, so that two combiners
                       reading different things cannot agree by accident. All
                       equal would make every configuration look identical and
                       the check would pass vacuously. */
                    dkr_combiner_inputs in;
                    float oi[4], oj[4];
                    int q;
                    memset(&in, 0, sizeof(in));
                    for (q = 0; q < 4; q++) {
                        in.texel0[q]      = 10.0f;
                        in.texel1[q]      = 30.0f;
                        in.primitive[q]   = 70.0f;
                        in.shade[q]       = 130.0f;
                        in.environment[q] = 190.0f;
                        in.combined[q]    = 250.0f;
                    }
                    dkr_combiner_eval(&ci, 0, &in, oi);
                    dkr_combiner_eval(&cj, 0, &in, oj);
                    if (memcmp(oi, oj, sizeof(oi)) != 0) { collisions++; }
                }
            }
        }
        check("no key collision between configurations that compute differently",
              collisions == 0);
    }

    /* --- The other modes --------------------------------------------------- */
    {
        dkr_rdp_state s;
        /* G_CYC_2CYCLE = 1 << 20; G_TF_BILERP = 2 << 12; G_TP_PERSP = 1 << 19 */
        dkr_rdp_decode_othermode((1u << 20) | (2u << 12) | (1u << 19), 0u, &s);
        check("two-cycle mode is recognised",  s.cycle  == DKR_CYCLE_2);
        check("bilinear filtering too",        s.filter == DKR_FILTER_BILINEAR);
        check("and texture perspective",       s.texture_persp == 1);

        dkr_rdp_decode_othermode(0u, 0u, &s);
        check("one cycle by default",          s.cycle  == DKR_CYCLE_1);
        check("point filtering by default",    s.filter == DKR_FILTER_POINT);

        /* G_TF_AVERAGE is 3, and **there is no value 1**. Treating the field as
           a boolean would give bilinear for AVERAGE — almost right, hence hard
           to spot. */
        dkr_rdp_decode_othermode(3u << 12, 0u, &s);
        check("G_TF_AVERAGE is not mistaken for point sampling",
              s.filter == DKR_FILTER_BILINEAR);

        dkr_rdp_decode_othermode(0u, (1u << 4) | (1u << 5), &s);
        check("the depth test reads back",     s.z_test  == 1);
        check("and its write too",             s.z_write == 1);
    }

    /* --- The translation to the abstract state ----------------------------- */
    {
        dkr_rdp_state    s;
        dkr_render_state r;
        dkr_combiner     c;
        int exact = -1;

        /* G_CC_MODULATEIDECALA: (TEXEL0, 0, SHADE, 0), alpha (0,0,0,TEXEL0).
           Texel and shade, texel alpha: that is the texture-alpha mode. */
        memset(&s, 0, sizeof(s));
        memset(&c, 0, sizeof(c));
        c.rgb[0].a = DKR_CC_TEXEL0; c.rgb[0].c = DKR_CC_SHADE;
        c.alpha[0].d = DKR_CC_TEXEL0;
        s.combiner = c; s.cycle = DKR_CYCLE_1;
        dkr_rdp_to_render_state(&s, &r, &exact);
        check("texel + shade + texel alpha translates",
              r.combine == DKR_COMBINE_TEXTURE_SHADE_ALPHA);
        check("and the translation declares itself exact", exact == 1);

        /* Two texels: the interface cannot express it, and the translation must
           **give warning**. An approximate translation that does not announce
           itself produces a plausible, wrong image — the worst outcome. */
        c.rgb[1].a = DKR_CC_TEXEL1;
        s.combiner = c;
        dkr_rdp_to_render_state(&s, &r, &exact);
        check("two texels: the translation declares itself approximate", exact == 0);

        /* Two-cycle mode too. */
        memset(&c, 0, sizeof(c));
        c.rgb[0].a = DKR_CC_TEXEL0; c.rgb[0].c = DKR_CC_SHADE;
        s.combiner = c; s.cycle = DKR_CYCLE_2;
        dkr_rdp_to_render_state(&s, &r, &exact);
        check("two-cycle mode: likewise", exact == 0);
    }

    /* --- The safety net ------------------------------------------------------ *
     *
     * The catalogue is `CC_TABLE`, generated from the game's source; the
     * eight-entry hand-written one this section used to exercise is gone,
     * having been transcribed in a shorthand where `0` meant zero. `test_combiner`
     * covers the table's own properties; what belongs here is that a decoded
     * combiner reaches it, since that is the join the machine found broken. */
    {
        dkr_combiner c;
        memset(&c, 0, sizeof(c));
        /* A configuration nobody has catalogued. */
        c.rgb[0].a = 13; c.rgb[0].b = 11; c.rgb[0].c = 29; c.rgb[0].d = 6;
        check("an unknown configuration is not catalogued",
              dkr_cc_lookup(dkr_rdp_combiner_key(&c, DKR_CYCLE_1)) == 0);

        /* **The join, in the direction the game exercises it.** A combiner is
           decoded from the words the game sends, and its key must find the entry.
           Recorded on the machine on 16 August 2026 and reported unknown by a
           counter asking the wrong table:
             13FFF041  rgb0=(1,15,4,7) a0=(7,7,7,1) rgb1=(5,0,12,0) a1=(0,7,3,7)
           Written here as the composition rather than as the key, so that this
           still checks something if the key's layout ever changes. */
        memset(&c, 0, sizeof(c));
        c.rgb[0].a = 1;   c.rgb[0].b = 15;  c.rgb[0].c = 4;   c.rgb[0].d = 7;
        c.alpha[0].a = 7; c.alpha[0].b = 7; c.alpha[0].c = 7; c.alpha[0].d = 1;
        c.rgb[1].a = 5;   c.rgb[1].b = 0;   c.rgb[1].c = 12;  c.rgb[1].d = 0;
        c.alpha[1].a = 0; c.alpha[1].b = 7; c.alpha[1].c = 3; c.alpha[1].d = 7;
        check("a configuration recorded from the game finds its entry",
              dkr_cc_lookup(dkr_rdp_combiner_key(&c, DKR_CYCLE_2)) != 0);

        /* Every entry of the catalogue must be found again by its own key.
           Otherwise the table could be full and the lookup still useless -
           which is exactly the state the machine was in. */
        {
            int n = dkr_cc_table_count(), q, found = 0;
            for (q = 0; q < n; q++) {
                const dkr_cc_entry *e = dkr_cc_table_at(q);
                if (e && dkr_cc_lookup(dkr_cc_entry_key(e)) != 0) { found++; }
            }
            sprintf(label, "all %d catalogued entries are found by their key", n);
            check(label, n > 0 && found == n);
        }
    }

    /* --- Fog, deduced from the blender -------------------------------------- *
     *
     * `G_RM_FOG_SHADE_A` is `GBL_c1(G_BL_CLR_FOG, G_BL_A_SHADE, ...)`, that is,
     * source 3 at position 30 and factor 2 at position 26. It is DKR's most
     * frequent render mode, and the decoder left it at zero. */
    {
        dkr_rdp_state s;
        const unsigned int FOG_SHADE_A = (3u << 30) | (2u << 26);
        memset(&s, 0, sizeof(s));
        dkr_rdp_decode_othermode(0u, FOG_SHADE_A, &s);
        check("G_RM_FOG_SHADE_A is recognised", s.fog != 0);

        /* And the check that stops fog being declared everywhere: the value 3
           means `G_BL_CLR_FOG` at position m1a but `G_BL_0` at position m1b. A
           single dictionary would let this case through. */
        memset(&s, 0, sizeof(s));
        dkr_rdp_decode_othermode(0u, (3u << 30) | (3u << 26), &s);
        check("but the value 3 at position m1b does not trigger it", s.fog == 0);

        memset(&s, 0, sizeof(s));
        dkr_rdp_decode_othermode(0u, 0u, &s);
        check("and a zero blender has no fog", s.fog == 0);
    }

    printf("\n%d failure(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d failure(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
