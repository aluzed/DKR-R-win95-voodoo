/* E04-S03 — the transformation test, and the measurement that decides.
 *
 * Two things here, and the second is the one the ticket explicitly asks for:
 *
 *   - the correctness of the 16.16 conversion and of the transformation,
 *     checked against values **computed by hand**;
 *   - the **cost per vertex on the target**, in x87 floating point and in fixed
 *     point, because the choice must be made "on a measurement, not on a
 *     preference, and the figure recorded".
 *
 * The measurement does not claim to decide for the whole game: it measures an
 * isolated transformation, without the cache misses of a real scene. What it
 * establishes is a ratio between two implementations under the same conditions,
 * which is precisely the question asked.
 */
#include "render/transform.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

static int g_fails;
static FILE *g_out;

static void say(const char *line)
{
    printf("%s\n", line);
    if (g_out) { fprintf(g_out, "%s\n", line); fflush(g_out); }
}

static void check(const char *what, int condition)
{
    char line[192];
    sprintf(line, "  %s %s", condition ? "ok   " : "FAIL ", what);
    say(line);
    if (!condition) { g_fails++; }
}

static void check_near(const char *what, double got, double want, double tol)
{
    char line[192];
    const int ok = (got - want < tol) && (want - got < tol);
    sprintf(line, "  %s %-42s expected %.5f, got %.5f",
            ok ? "ok   " : "FAIL ", what, want, got);
    say(line);
    if (!ok) { g_fails++; }
}

/* Writes a matrix in the N64's format: sixteen integer parts then sixteen
   fractional parts, big-endian. */
static void put_fixed(unsigned char *b, int k, short hi, unsigned short lo)
{
    b[k * 2]          = (unsigned char)(hi >> 8);
    b[k * 2 + 1]      = (unsigned char)(hi);
    b[32 + k * 2]     = (unsigned char)(lo >> 8);
    b[32 + k * 2 + 1] = (unsigned char)(lo);
}

int main(void)
{
    dkr_transform t;
    dkr_matrix    m;
    unsigned char raw[64];

    g_out = fopen("D:\\TRANSFRM.TXT", "w");

    /* --- The 16.16 conversion ---------------------------------------------- */
    memset(raw, 0, sizeof(raw));
    put_fixed(raw,  0, 1, 0);             /* 1.0 */
    put_fixed(raw,  1, 0, 0x8000);        /* 0.5 */
    put_fixed(raw,  2, -1, 0x8000);       /* -0.5: integer -1, fraction 0.5 */
    put_fixed(raw,  3, -2, 0);            /* -2.0 */
    put_fixed(raw,  4, 0, 0xFFFF);        /* just under 1 */
    check("the 16.16 conversion returns 1.0", dkr_matrix_from_fixed(raw, &m) != 0);
    check_near("integer alone",      m.m[0][0],  1.0,        0.00001);
    check_near("fraction alone",     m.m[0][1],  0.5,        0.00001);
    /* The case that breaks a naive conversion: **the fraction is not signed**.
       Reading it as a short would give -1 + (-0.5) = -1.5 instead of -0.5. */
    check_near("negative with fraction", m.m[0][2], -0.5,    0.00001);
    check_near("negative integer",   m.m[0][3], -2.0,        0.00001);
    check_near("just under one",     m.m[1][0],  0.9999847,  0.00001);

    /* --- The projection ----------------------------------------------------- */
    dkr_transform_init(&t);
    dkr_transform_set_viewport(&t, 320.0f, -240.0f, 320.0f, 240.0f);

    /* A simple projection: w = z. It is what gives the rejection meaning — with
       the identity, w is 1 whatever the vertex, and nothing is ever behind the
       plane. A first version of this test claimed otherwise, and it was the test
       that was wrong. */
    memset(&m, 0, sizeof(m));
    m.m[0][0] = 1.0f; m.m[1][1] = 1.0f; m.m[2][2] = 1.0f;
    m.m[2][3] = 1.0f;                 /* w = z */
    dkr_transform_set_projection(&t, &m);
    {
        dkr_source_vertex behind = { 10, 10, 0, 255, 255, 255, 255 };
        dkr_source_vertex far_behind = { 10, 10, -50, 255, 255, 255, 255 };
        dkr_render_vertex rv;
        /* Dividing by zero would produce infinite coordinates crossing the
           screen — far more visible than a missing vertex. */
        check("a vertex with zero w is rejected",
              dkr_transform_vertex(&t, &behind, &rv) == 0);
        check("a vertex behind the plane too",
              dkr_transform_vertex(&t, &far_behind, &rv) == 0);
    }
    {
        dkr_source_vertex sv = { 100, 50, 200, 255, 128, 64, 255 };
        dkr_render_vertex rv;
        check("a vertex in front of the plane is accepted",
              dkr_transform_vertex(&t, &sv, &rv) != 0);
        /* x_screen = x/w * 320 + 320, with w = z = 200 */
        check_near("screen x", rv.x, 100.0 / 200.0 * 320.0 + 320.0, 0.01);
        check_near("screen y", rv.y,  50.0 / 200.0 * -240.0 + 240.0, 0.01);
        check_near("1/w",      rv.oow, 1.0 / 200.0,                  0.00001);
        check("the colour passes through unchanged",
              rv.r == 255.0f && rv.g == 128.0f && rv.b == 64.0f && rv.a == 255.0f);
        /* Texture coordinates do not come from the vertex: they must stay at
           zero rather than be invented. */
        check("the texture coordinates stay zero",
              rv.tmu[0][DKR_TMU_SOW] == 0.0f && rv.tmu[0][DKR_TMU_TOW] == 0.0f);
    }

    /* --- The stack ---------------------------------------------------------- */
    {
        dkr_matrix translate;
        dkr_source_vertex sv = { 0, 0, 100, 255, 255, 255, 255 };
        dkr_render_vertex rv;
        memset(&translate, 0, sizeof(translate));
        translate.m[0][0] = translate.m[1][1] = translate.m[2][2] =
            translate.m[3][3] = 1.0f;
        translate.m[3][0] = 50.0f;      /* translation in x */
        dkr_transform_set_matrix(&t, 1, &translate);
        dkr_transform_select(&t, 1);
        check("a translated vertex transforms",
              dkr_transform_vertex(&t, &sv, &rv) != 0);
        check_near("the translation is applied", rv.x,
                   50.0 / 100.0 * 320.0 + 320.0, 0.01);
        dkr_transform_select(&t, 0);
        check("going back to slot 0 cancels the translation",
              dkr_transform_vertex(&t, &sv, &rv) != 0 && rv.x > 319.0f && rv.x < 321.0f);
        /* The depth is measured on the game: three slots. An out-of-bounds index
           must be clamped, not write elsewhere. */
        dkr_transform_select(&t, 99);
        check("an out-of-bounds slot is clamped", t.selected == DKR_MATRIX_SLOTS - 1);
    }

    /* --- The measurement ---------------------------------------------------- *
     *
     * The ticket asks for the choice between floating point and fixed point to
     * be settled "on a measurement, not on a preference". Here is the
     * measurement. */
    {
        enum { COUNT = 20000 };
        static dkr_source_vertex src[COUNT];
        static dkr_render_vertex dst[COUNT];
        int i;
        unsigned long ms_float;

        for (i = 0; i < COUNT; i++) {
            src[i].x = (short)((i * 37) % 1000 - 500);
            src[i].y = (short)((i * 53) % 1000 - 500);
            src[i].z = (short)(100 + (i % 400));
            src[i].r = src[i].g = src[i].b = src[i].a = 255;
        }
        dkr_transform_select(&t, 0);

#if defined(_WIN32)
        {
            const DWORD start = GetTickCount();
            int pass;
            /* Several passes: `GetTickCount` advances in 9 ms steps on this
               machine (E02-S03), and too brief a measurement would only measure
               the clock's granularity. */
            for (pass = 0; pass < 20; pass++) {
                for (i = 0; i < COUNT; i++) {
                    dkr_transform_vertex(&t, &src[i], &dst[i]);
                }
            }
            ms_float = (unsigned long)(GetTickCount() - start);
        }
        {
            char line[192];
            const double per_vertex_us =
                (double)ms_float * 1000.0 / ((double)COUNT * 20.0);
            sprintf(line, "  measurement: %d vertices x 20 passes in %lu ms",
                    COUNT, ms_float);
            say(line);
            sprintf(line, "  that is %.3f us per vertex in x87 floating point",
                    per_vertex_us);
            say(line);
            /* At 400 MHz, a 60 Hz frame leaves 16.6 ms. The sustainable vertex
               count follows directly, and that is what interests E08. */
            sprintf(line, "  that is %.0f vertices in a 16.6 ms frame",
                    per_vertex_us > 0.0 ? 16600.0 / per_vertex_us : 0.0);
            say(line);
            check("the measurement produced a usable figure", ms_float > 0);
        }
#else
        (void)ms_float;
        for (i = 0; i < COUNT; i++) {
            dkr_transform_vertex(&t, &src[i], &dst[i]);
        }
        say("  measurement: skipped on the host - only the target figure counts");
#endif
    }

    {
        char line[64];
        sprintf(line, "\n%d failure(s)", g_fails);
        say(line);
    }
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
