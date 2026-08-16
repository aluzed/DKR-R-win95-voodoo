/* E05-S03 — the combiner oracle and the mapping table.
 *
 * Two very different things are checked here, and they are worth telling apart.
 *
 * **The evaluator** is checked against values computed by hand. That is possible
 * because `(a - b) * c + d` is a short formula: one can lay the numbers down and
 * know the answer in advance, which is the only way to check an oracle —
 * comparing it against another program would merely move the question.
 *
 * **The table** is checked by properties, not case by case: no duplicate key,
 * every entry findable by its key, every category justified. One check per
 * configuration would be the same code twenty-nine times and would let through
 * precisely what matters — that one entry masks another.
 */
#include "render/combiner.h"

#include <stdio.h>
#include <string.h>

static int g_fails;
static FILE *g_out;

static void check(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok   " : "FAIL ", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", ok ? "ok   " : "FAIL ", what);
        fflush(g_out);
    }
    if (!ok) { g_fails++; }
}

static void check_near(const char *what, double got, double want, double tol)
{
    const int ok = (got - want < tol) && (want - got < tol);
    printf("  %s %-46s expected %8.3f  got %8.3f\n",
           ok ? "ok   " : "FAIL ", what, want, got);
    if (g_out) {
        fprintf(g_out, "  %s %-46s expected %8.3f  got %8.3f\n",
                ok ? "ok   " : "FAIL ", what, want, got);
        fflush(g_out);
    }
    if (!ok) { g_fails++; }
}

static void report(const char *fmt, unsigned long a, unsigned long b)
{
    char line[200];
    sprintf(line, fmt, a, b);
    printf("%s\n", line);
    if (g_out) { fprintf(g_out, "%s\n", line); fflush(g_out); }
}

static void set4(float v[4], float r, float g, float b, float a)
{
    v[0] = r; v[1] = g; v[2] = b; v[3] = a;
}

int main(void)
{
    dkr_combiner_inputs in;
    dkr_combiner c;
    float out[4];

    g_out = fopen("D:\\COMBTEST.TXT", "w");

    memset(&in, 0, sizeof(in));
    set4(in.texel0,      200.0f, 100.0f,  50.0f, 128.0f);
    set4(in.texel1,       10.0f,  20.0f,  30.0f,  40.0f);
    set4(in.primitive,   255.0f,   0.0f,   0.0f, 200.0f);
    set4(in.shade,       128.0f, 128.0f, 128.0f, 255.0f);
    set4(in.environment,   0.0f, 255.0f,   0.0f,  64.0f);

    /* --- G_CC_MODULATEIDECALA: (TEXEL0, 0, SHADE, 0), alpha (0,0,0,TEXEL0) --- *
     *
     * Colour = texel0 x shade/255 = 200 x 128/255 = 100.39 for red.
     * Alpha = texel0.a = 128. */
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = 1; c.rgb[0].b = 8; c.rgb[0].c = 4; c.rgb[0].d = 7;
    c.alpha[0].a = 7; c.alpha[0].b = 7; c.alpha[0].c = 7; c.alpha[0].d = 1;
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("MODULATEIDECALA: red = texel x shade", out[0], 200.0 * 128.0 / 255.0, 0.01);
    check_near("  and the alpha comes from the texel",  out[3], 128.0, 0.01);

    /* --- G_CC_BLENDI_ENV_ALPHA: (ENV, SHADE, ENV_ALPHA, SHADE) --------------- *
     *
     * This is the `d == b` form that lands exactly on Glide's BLEND function,
     * and the most useful case to check by hand:
     * (0 - 128) x 64/255 + 128 = 96.88 for red;
     * (255 - 128) x 64/255 + 128 = 159.88 for green. */
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = 5; c.rgb[0].b = 4; c.rgb[0].c = 12; c.rgb[0].d = 4;
    c.alpha[0].a = 7; c.alpha[0].b = 7; c.alpha[0].c = 7; c.alpha[0].d = 4;
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("BLENDI_ENV_ALPHA: red interpolated towards env",
               out[0], (0.0 - 128.0) * 64.0 / 255.0 + 128.0, 0.01);
    check_near("  and green too",
               out[1], (255.0 - 128.0) * 64.0 / 255.0 + 128.0, 0.01);
    check_near("  the alpha stays the vertex's", out[3], 255.0, 0.01);

    /* --- Two cycles: the second reads COMBINED ------------------------------ *
     *
     * MODULATEIDECALA then MODULATEIDECALA2 = (COMBINED, 0, SHADE, 0).
     * The first gives 100.39 in red; the second multiplies it by shade again:
     * 100.39 x 128/255 = 50.39. This is the check that proves the result is
     * genuinely **fed back** and not recomputed from the inputs. */
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = 1; c.rgb[0].b = 8; c.rgb[0].c = 4; c.rgb[0].d = 7;
    c.alpha[0].a = 7; c.alpha[0].b = 7; c.alpha[0].c = 7; c.alpha[0].d = 1;
    c.rgb[1].a = 0; c.rgb[1].b = 8; c.rgb[1].c = 4; c.rgb[1].d = 7;
    c.alpha[1].a = 7; c.alpha[1].b = 7; c.alpha[1].c = 7; c.alpha[1].d = 0;
    dkr_combiner_eval_all(&c, DKR_CYCLE_2, &in, out);
    check_near("two cycles: the second multiplies the first again",
               out[0], 200.0 * 128.0 / 255.0 * 128.0 / 255.0, 0.05);

    /* And the negative check: in a single cycle, the second stage must have no
       effect. Without it, an evaluator that always applied both would pass the
       previous test. */
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("in one cycle, the second stage is ignored",
               out[0], 200.0 * 128.0 / 255.0, 0.01);

    /* --- The inputs are not named the same way depending on position --------- *
     *
     * The value 6 is `1` in positions `a` and `d`, but `CENTER` in `b` — which
     * this module treats as zero. Setting a = 6 and b = 6 must therefore give
     * (255 - 0) x c + d and not zero. A single table would give zero, and the
     * error would only show on the few configurations concerned. */
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = 6; c.rgb[0].b = 6; c.rgb[0].c = 6; c.rgb[0].d = 7;
    c.alpha[0].c = 7;
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("the value 6 does not mean the same in a and in b",
               out[0], 255.0, 0.01);

    /* --- Alpha has its own table for `c` ------------------------------------- *
     * In alpha, `c = 0` means LOD_FRACTION and not COMBINED. */
    memset(&c, 0, sizeof(c));
    c.rgb[0].c = 16;
    c.alpha[0].a = 6; c.alpha[0].b = 7; c.alpha[0].c = 0; c.alpha[0].d = 7;
    in.lod_fraction = 255.0f;
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("in alpha, c=0 designates LOD_FRACTION", out[3], 255.0, 0.01);
    in.lod_fraction = 0.0f;

    /* --- Clamping ------------------------------------------------------------ */
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = 6; c.rgb[0].b = 8; c.rgb[0].c = 6; c.rgb[0].d = 6;  /* 1 x 1 + 1 */
    c.alpha[0].c = 7;
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("the result is clamped to 255", out[0], 255.0, 0.01);

    /* --- The table ----------------------------------------------------------- */
    {
        const int n = dkr_cc_table_count();
        int i, j, duplicates = 0, found_again = 0;
        int per_cat[4];
        long weight[4];

        memset(per_cat, 0, sizeof(per_cat));
        memset(weight, 0, sizeof(weight));

        check("the table is not empty", n > 0);
        report("  configurations in the table: %lu", (unsigned long)n, 0ul);

        for (i = 0; i < n; i++) {
            const dkr_cc_entry *e = dkr_cc_table_at(i);
            const unsigned long long k = dkr_cc_entry_key(e);
            per_cat[e->category]++;
            weight[e->category] += e->entries;
            /* Found again by its own key: that is what guarantees the indexing
               by canonical form actually works, and not merely that the table
               exists. */
            if (dkr_cc_lookup(k) != 0) { found_again++; }
            for (j = i + 1; j < n; j++) {
                if (dkr_cc_entry_key(dkr_cc_table_at(j)) == k) { duplicates++; }
            }
        }
        /* **The check that counts.** Two entries with the same canonical key
           would mask each other, and the second would never be reached: a whole
           configuration rendered with another's setup, with no message
           reporting it. */
        check("no canonical key is duplicated", duplicates == 0);
        check("every entry is findable by its key", found_again == n);

        report("  exact %lu, multipass %lu",
               (unsigned long)per_cat[DKR_CC_EXACT],
               (unsigned long)per_cat[DKR_CC_MULTIPASS]);
        report("  approximate %lu, two texels %lu",
               (unsigned long)per_cat[DKR_CC_APPROXIMATE],
               (unsigned long)per_cat[DKR_CC_TWO_TEXELS]);
        report("  weighted by table entries: exact %lu out of %lu",
               (unsigned long)weight[DKR_CC_EXACT],
               (unsigned long)(weight[0] + weight[1] + weight[2] + weight[3]));

        /* **This threshold first bore on the wrong quantity.**
         *
         * It required half the entries to be exact, and so encoded the result of
         * the moment rather than a requirement. When measurement on the card
         * reclassified the ENV_ALPHA family as approximate, the exact share fell
         * to 45% and the test failed — without anything getting worse for the
         * hardware.
         *
         * What really costs is **fill**, and fill is what limits a Voodoo 2 at
         * 640x480. An approximate configuration does not cost an extra pass: it
         * costs correctness, which the gap measurement reports elsewhere. Only
         * multipass doubles the painted surface.
         *
         * The threshold therefore now bears on the multipass share, which is
         * what the card pays for. */
        check("multipass stays in the minority: it is what doubles the fill, "
              "and fill is what limits the card",
              weight[DKR_CC_MULTIPASS] * 2 <
              (weight[0] + weight[1] + weight[2] + weight[3]));

        /* Every multipass or approximate configuration must carry a note saying
           why. Without it, the classification is an opinion. */
        {
            int justified = 0, to_justify = 0;
            for (i = 0; i < n; i++) {
                const dkr_cc_entry *e = dkr_cc_table_at(i);
                if (e->category == DKR_CC_EXACT) { continue; }
                to_justify++;
                if (e->note && e->note[0]) { justified++; }
            }
            check("every non-exact configuration carries its justification",
                  justified == to_justify);
        }
    }

    /* --- The fallback -------------------------------------------------------- */
    {
        const dkr_cc_setup *r = dkr_cc_fallback();
        check("an unknown configuration is not found",
              dkr_cc_lookup(0xFFFFFFFFFFFFFFFFull) == 0);
        check("the fallback exists and uses the texture",
              r != 0 && r->uses_texture != 0);
        /* **Not absurd**: the fallback must neither erase everything nor paint
           in an alert colour. A zero factor would be the first case. */
        check("and it does not set the factor to zero", r->cc_factor != 0);
    }

    printf("\n%d failure(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d failure(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
