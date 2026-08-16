/* Quelles valeurs ont reellement GR_COMBINE_FUNCTION_* et GR_COMBINE_FACTOR_* ?
 *
 * Le harnais de E05-S03 a montre un ecart de 140 a 156 unites sur toute la
 * famille BLEND. Ces valeurs-la sont les seules du projet a n'avoir jamais ete
 * mesurees : `glide_state_probe.c` a verifie le melange, la profondeur, les
 * ciseaux et le test alpha, mais pas le combineur.
 *
 * On ne devine donc pas : on balaie. Pour chaque valeur de fonction, on dessine
 * avec des entrees connues et l'on relit le pixel. La fonction cherchee est
 * celle qui produit `f x (other - local) + local` — et le balayage dira aussi ce
 * que font les autres, ce qui vaut d'etre consigne.
 */
#include "render/glide.h"
#include "render/backend.h"
#include "render/combiner.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static FILE *g_out;
static void say(const char *fmt, ...)
{
    char l[256]; va_list ap; va_start(ap, fmt); vsprintf(l, fmt, ap); va_end(ap);
    if (g_out) { fputs(l, g_out); fflush(g_out); }
}

/* Entrees choisies pour que chaque formule candidate donne un resultat
   distinct. `other` est la constante, `local` la couleur du sommet, et le
   facteur vaudra l'alpha de la constante — trois valeurs bien separees. */
#define OTH_R 40
#define OTH_G 80
#define OTH_B 120
#define OTH_A 64        /* facteur = 64/255 = 0,251 */
#define LOC_R 200
#define LOC_G 160
#define LOC_B 240

static unsigned g_px[640 * 480];

static void quad(dkr_render_backend *bk, int w, int h)
{
    dkr_render_vertex v[6];
    const float xs[6] = {0,(float)w,(float)w,0,(float)w,0};
    const float ys[6] = {0,0,(float)h,0,(float)h,(float)h};
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = LOC_R; v[i].g = LOC_G; v[i].b = LOC_B; v[i].a = 255.0f;
        v[i].oow = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state st;
    int fn, rw = 0, rh = 0;
    const int W = 640, H = 480;

    g_out = fopen("D:\\CCENUM.TXT", "w");
    say("balayage des fonctions de combineur de Glide\n\n");
    say("  entrees : other = constante (%d,%d,%d) alpha %d\n",
        OTH_R, OTH_G, OTH_B, OTH_A);
    say("            local = couleur du sommet (%d,%d,%d)\n\n",
        LOC_R, LOC_G, LOC_B);
    say("  candidates, en rouge :\n");
    say("    LOCAL                     = %d\n", LOC_R);
    say("    SCALE_OTHER (f x other)   = %d\n", OTH_R * OTH_A / 255);
    say("    SCALE_OTHER_ADD_LOCAL     = %d\n", OTH_R * OTH_A / 255 + LOC_R);
    say("    BLEND f(other-local)+local= %d\n",
        (OTH_R - LOC_R) * OTH_A / 255 + LOC_R);
    say("\n-- balayage du FACTEUR, fonction BLEND (7) --\n");
    say("%-4s %-8s %s\n", "fac", "lu", "interpretation");

    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("ECHEC ouverture\n"); return 1; }
    memset(&st, 0, sizeof(st));
    st.blend = DKR_BLEND_OPAQUE; st.depth = DKR_DEPTH_DISABLED;
    st.cull = DKR_CULL_NONE;
    bk.set_state(bk.self, &st);

    for (fn = 0; fn <= 15; fn++) {
        dkr_cc_setup r;
        unsigned c;
        int lu, attendu_blend, attendu_scale, attendu_local, attendu_add;
        memset(&r, 0, sizeof(r));
        /* facteur = OTHER_ALPHA (candidat 2), local itere, other constant */
        r.cc_function = 7; r.cc_factor = (unsigned char)fn;
        r.cc_local = 0; r.cc_other = 2;
        r.ac_function = 1; r.ac_factor = 8; r.ac_local = 0; r.ac_other = 0;
        r.uses_texture = 0;

        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        dkr_glide_backend_set_recipe(&r,
            ((unsigned)OTH_A << 24) | ((unsigned)OTH_R << 16) |
            ((unsigned)OTH_G << 8) | (unsigned)OTH_B);
        quad(&bk, W, H);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) <= 0) { continue; }
        c = g_px[(size_t)(rh/2) * (size_t)rw + (size_t)(rw/2)];
        lu = (int)((c >> 16) & 0xFF);

        attendu_local = LOC_R;
        attendu_scale = OTH_R * OTH_A / 255;
        attendu_add   = attendu_scale + LOC_R;
        attendu_blend = (OTH_R - LOC_R) * OTH_A / 255 + LOC_R;
        say("%-4d %06X   %s\n", fn, c & 0x00FFFFFFu,
            (lu > attendu_blend - 12 && lu < attendu_blend + 12) ? "<== BLEND" :
            (lu > attendu_local - 12 && lu < attendu_local + 12) ? "LOCAL" :
            (lu > attendu_scale - 12 && lu < attendu_scale + 12) ? "SCALE_OTHER" :
            (lu > attendu_add   - 12 && lu < attendu_add   + 12) ? "SCALE_OTHER_ADD_LOCAL" :
            (lu == 0) ? "zero" : "autre");
    }
    bk.close(bk.self);
    say("\nfin\n");
    if (g_out) { fclose(g_out); }
    return 0;
}
