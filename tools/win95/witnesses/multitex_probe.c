/* E05-S04 — le chaînage des deux TMU, mesuré plutôt que supposé.
 *
 * `grTexCombine` chaîne la sortie de la TMU 1 vers la TMU 0. Ses fonctions
 * forment une liste close — `DECAL`, `OTHER`, `ADD`, `MULTIPLY`, une
 * interpolation — dont les valeurs d'énumération sont, comme tout le reste de
 * Glide sur cette machine, écrites de mémoire.
 *
 * Ce projet a déjà payé deux fois pour avoir supposé de telles valeurs : le sens
 * de comparaison du tampon w, puis toute la famille `BLENDI` du combineur de
 * couleurs. On balaie donc.
 *
 * ## Deux textures faites pour se distinguer
 *
 * La TMU 1 porte du rouge pur, la TMU 0 du bleu pur. Chaque fonction candidate
 * produit alors une couleur qui la nomme sans ambiguïté :
 *
 *     DECAL     -> bleu    (la TMU 0 seule)
 *     OTHER     -> rouge   (la TMU 1 seule)
 *     ADD       -> magenta (les deux)
 *     MULTIPLY  -> noir    (rouge x bleu = 0)
 *     lerp      -> un mélange, selon le facteur
 *
 * Des textures qui se ressembleraient rendraient le balayage muet : c'est le
 * même principe que le damier à quatre couleurs de E05-S02, et la raison en est
 * la même — une épreuve doit distinguer les fautes, pas seulement réussir.
 *
 * ## Et la cohérence des coordonnées
 *
 * Chaque TMU a son propre jeu de coordonnées dans le sommet Glide. Une erreur
 * ici décale les deux couches l'une par rapport à l'autre, ce qui **ressemble à
 * un défaut de combineur** et se diagnostique très mal. Le témoin le vérifie
 * séparément, sur deux textures dont les motifs doivent se superposer
 * exactement.
 */
#include "render/glide.h"
#include "render/backend.h"
#include "render/combiner.h"
#include "render/tmu.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static FILE *g_out;
static int   g_fails;

static void say(const char *fmt, ...)
{
    char l[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(l, fmt, ap);
    va_end(ap);
    if (g_out) { fputs(l, g_out); fflush(g_out); }
}

static void check(const char *what, int ok)
{
    say("  %s %s\n", ok ? "ok   " : "ECHEC", what);
    if (!ok) { g_fails++; }
}

#define TW 64
#define TH 64
static unsigned short g_rouge[TW * TH];
static unsigned short g_bleu[TW * TH];
static unsigned short g_bandes0[TW * TH];
static unsigned short g_bandes1[TW * TH];
static unsigned g_px[640 * 480];

static void build_textures(void)
{
    int x, y;
    for (y = 0; y < TH; y++) {
        for (x = 0; x < TW; x++) {
            g_rouge[y * TW + x] = (unsigned short)(0x8000u | (31u << 10));
            g_bleu [y * TW + x] = (unsigned short)(0x8000u | 31u);
            /* Deux motifs complementaires : la moitie gauche pleine sur l'une,
               la moitie droite sur l'autre. Superposees, elles couvrent tout ;
               decalees, elles laissent une bande. */
            g_bandes0[y * TW + x] = (unsigned short)
                (0x8000u | ((x < TW / 2) ? (31u << 10) : 0u));
            g_bandes1[y * TW + x] = (unsigned short)
                (0x8000u | ((x >= TW / 2) ? 31u : 0u));
        }
    }
}

static void quad(dkr_render_backend *bk, int w, int h)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0, (float)w, (float)w, 0, (float)w, 0 };
    const float ys[6] = { 0, 0, (float)h, 0, (float)h, (float)h };
    const float ss[6] = { 0, 256.0f, 256.0f, 0, 256.0f, 0 };
    const float ts[6] = { 0, 0, 256.0f, 0, 256.0f, 256.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = v[i].g = v[i].b = v[i].a = 255.0f;
        v[i].oow = 1.0f;
        /* **Les deux unités reçoivent les mêmes coordonnées**, et c'est ce qui
           est vérifié plus bas : chaque TMU a son propre jeu dans le sommet
           Glide, et les remplir séparément est l'occasion d'en oublier un. */
        v[i].tmu[0][DKR_TMU_SOW] = ss[i];
        v[i].tmu[0][DKR_TMU_TOW] = ts[i];
        v[i].tmu[0][DKR_TMU_OOW] = 1.0f;
        v[i].tmu[1][DKR_TMU_SOW] = ss[i];
        v[i].tmu[1][DKR_TMU_TOW] = ts[i];
        v[i].tmu[1][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

static unsigned lire(int x, int y, int w) { return g_px[(size_t)y * (size_t)w + (size_t)x] & 0x00FFFFFFu; }

static int proche(unsigned c, int r, int g, int b)
{
    const int cr = (int)((c >> 16) & 0xFF), cg = (int)((c >> 8) & 0xFF),
              cb = (int)(c & 0xFF), s = 100;
    return ((cr > s) == (r != 0)) && ((cg > s) == (g != 0)) && ((cb > s) == (b != 0));
}

static dkr_texture_handle charge(dkr_render_backend *bk, const void *px,
                                 unsigned long long cle, int tmu)
{
    dkr_texture_desc d;
    memset(&d, 0, sizeof(d));
    d.key = cle; d.format = DKR_TEXFMT_RGBA5551;
    d.width = TW; d.height = TH;
    d.pixels = px; d.size_bytes = (size_t)(TW * TH * 2);
    d.tmu = tmu;
    return bk->texture_upload(bk->self, &d);
}

int main(void)
{
    dkr_render_backend bk;
    dkr_render_state   st;
    dkr_glide_hardware hw;
    dkr_texture_handle h_rouge, h_bleu, h_b0, h_b1;
    const int W = 640, H = 480;
    int fn, rw = 0, rh = 0;

    g_out = fopen("D:\\MULTITEX.TXT", "w");
    say("chainage des deux TMU, mesure\n\n");

    if (dkr_glide_detect(&hw) != DKR_GLIDE_OK) {
        say("ECHEC : pas de carte\n"); return 1;
    }
    say("  TMU detectees : %d\n", hw.tmu_count);
    check("la carte en a bien deux", hw.tmu_count >= 2);

    build_textures();
    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) { say("ECHEC ouverture\n"); return 1; }
    bk.begin_frame(bk.self, 0x000000);

    h_rouge = charge(&bk, g_rouge, 0x1001ull, 1);   /* TMU 1 */
    h_bleu  = charge(&bk, g_bleu,  0x1002ull, 0);   /* TMU 0 */
    check("la texture de la TMU 1 se charge", h_rouge != 0);
    check("celle de la TMU 0 aussi",          h_bleu  != 0);

    /* Les deux allocateurs doivent avoir servi, chacun le sien. Si les deux
       textures atterrissaient sur la meme unite, tout ce qui suit mesurerait
       autre chose que le chainage. */
    {
        const dkr_tmu *t0 = dkr_glide_backend_tmu(0);
        const dkr_tmu *t1 = dkr_glide_backend_tmu(1);
        say("  occupation TMU0 %u o, TMU1 %u o\n",
            t0 ? dkr_tmu_used(t0) : 0u, t1 ? dkr_tmu_used(t1) : 0u);
        check("chaque unite porte exactement une texture",
              t0 && t1 && dkr_tmu_used(t0) == 8192u && dkr_tmu_used(t1) == 8192u);
    }

    memset(&st, 0, sizeof(st));
    /* **Sans ce mode, tout le balayage sort blanc.** Le combineur de couleurs
       ignore la sortie des TMU tant qu'on ne lui demande pas le texel : la
       premiere version de ce temoin laissait `combine` a SHADE, et les douze
       fonctions rendaient la couleur du sommet — un ecran blanc uniforme qui ne
       disait rien. La verification de coherence des coordonnees y « reussissait »
       d'ailleurs sans rien etablir, faute de pouvoir distinguer le fond du
       resultat. */
    st.combine = DKR_COMBINE_TEXTURE;
    st.blend = DKR_BLEND_OPAQUE; st.depth = DKR_DEPTH_DISABLED;
    st.cull = DKR_CULL_NONE; st.filter = DKR_FILTER_POINT;
    st.wrap_s = st.wrap_t = DKR_WRAP_CLAMP;
    st.texture = h_bleu; st.texture1 = h_rouge;

    say("\n-- balayage des fonctions de grTexCombine sur la TMU 0 --\n");
    say("  TMU1 = rouge pur, TMU0 = bleu pur\n");
    say("%-4s %-8s %s\n", "fn", "lu", "interpretation");

    for (fn = 0; fn <= 11; fn++) {
        unsigned c;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        dkr_glide_backend_chain(h_bleu, h_rouge, (unsigned char)fn, 8);
        quad(&bk, W, H);
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) <= 0) { continue; }
        c = lire(rw / 2, rh / 2, rw);
        say("%-4d %06X   %s\n", fn, c,
            proche(c, 0, 0, 1) ? "DECAL : la TMU 0 seule" :
            proche(c, 1, 0, 0) ? "OTHER : la TMU 1 seule" :
            proche(c, 1, 0, 1) ? "ADD : les deux" :
            (c == 0)           ? "MULTIPLY, ou zero" : "melange");
    }

    /* --- La cohérence des coordonnées ---------------------------------------- *
     *
     * Deux motifs complémentaires : la moitié gauche pleine sur l'un, la moitié
     * droite sur l'autre. Additionnés, ils doivent couvrir **tout** l'écran. Un
     * décalage entre les jeux de coordonnées des deux unités laisserait une
     * bande noire, ou en ferait apparaître une double.
     *
     * C'est la vérification que le ticket demande, et elle vaut d'être séparée :
     * un décalage se confond avec un défaut de combineur, et l'on cherche
     * longtemps du mauvais côté. */
    h_b0 = charge(&bk, g_bandes0, 0x2001ull, 0);
    h_b1 = charge(&bk, g_bandes1, 0x2002ull, 1);
    if (h_b0 && h_b1) {
        int noirs = 0, i, got;
        st.texture = h_b0; st.texture1 = h_b1;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        /* ADD : chaque moitié vient d'une unité différente.
         *
         * **La valeur est 4, et non 3.** Le balayage ci-dessus l'a etablie :
         * DECAL vaut 1, OTHER vaut 3, ADD vaut 4 — decalees d'un cran par
         * rapport a ce qui avait ete ecrit de memoire. Cette verification-ci
         * demandait donc OTHER en croyant demander ADD, et ne voyait qu'une
         * seule couche. C'est la troisieme fois de ce portage qu'une valeur
         * d'enumeration Glide supposee se revele fausse, et la troisieme fois
         * que seule la mesure le dit. */
        dkr_glide_backend_chain(h_b0, h_b1, 4, 8);
        quad(&bk, W, H);
        bk.present(bk.self);
        got = dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh);
        for (i = 0; i < got; i++) {
            if ((g_px[i] & 0x00FFFFFFu) == 0) { noirs++; }
        }
        say("\n-- coherence des coordonnees entre les deux unites --\n");
        say("  pixels noirs : %d sur %d\n", noirs, got);
        say("  quart gauche 0x%06X, quart droit 0x%06X\n",
            lire(rw / 4, rh / 2, rw), lire(rw * 3 / 4, rh / 2, rw));
        /* Une colonne de bord peut manquer ; une bande ne le peut pas. */
        check("les deux couches se superposent sans laisser de bande",
              got > 0 && noirs < got / 100);
        /* **Et le controle qui empeche un ecran uniforme de passer.** La moitie
           gauche vient d'une unite, la droite de l'autre : elles doivent donc
           differer. Sans cela, un blanc uniforme — le symptome exact de la
           premiere version de ce temoin — satisferait le controle precedent. */
        check("et chaque moitie vient bien d'une unite differente",
              lire(rw / 4, rh / 2, rw) != lire(rw * 3 / 4, rh / 2, rw));
    }

    /* --- Le repli a une TMU, et le gain --------------------------------------- *
     *
     * Deux passes additives donnent le meme resultat qu'un ADD chaine : la
     * premiere pose la couche de la TMU 0, la seconde ajoute celle de la TMU 1.
     * L'egalite n'est pas evidente et doit etre **verifiee par difference**, pas
     * supposee — c'est ce que le ticket demande. */
    if (h_b0 && h_b1) {
        static unsigned une_passe[640 * 480];
        int i, got, differents = 0;
        unsigned long t_une, t_deux;

        /* Une passe, deux TMU. */
        dkr_glide_backend_force_single_tmu(0);
        st.texture = h_b0; st.texture1 = h_b1;
        bk.begin_frame(bk.self, 0x000000);
        bk.set_state(bk.self, &st);
        dkr_glide_backend_chain(h_b0, h_b1, 4, 8);
        quad(&bk, W, H);
        bk.present(bk.self);
        got = dkr_glide_read_framebuffer(une_passe, W * H, &rw, &rh);

        /* Deux passes, une TMU forcee. La seconde passe ajoute. */
        dkr_glide_backend_force_single_tmu(1);
        bk.begin_frame(bk.self, 0x000000);
        st.texture = h_b0; st.texture1 = 0;
        bk.set_state(bk.self, &st);
        quad(&bk, W, H);
        {
            dkr_render_state st2 = st;
            st2.blend = DKR_BLEND_ADDITIVE;
            st2.texture = h_b1;
            bk.set_state(bk.self, &st2);
            /* La texture de la seconde couche reside sur la TMU 1 ; en repli,
               c'est la TMU 0 qui doit l'echantillonner. On la recharge donc a
               son adresse — la duplication est le prix du repli, et c'est
               precisement pourquoi il n'est pas le chemin par defaut. */
            {
                dkr_texture_handle h = charge(&bk, g_bandes1, 0x2003ull, 0);
                if (h) { st2.texture = h; bk.set_state(bk.self, &st2); }
            }
            quad(&bk, W, H);
        }
        bk.present(bk.self);
        if (dkr_glide_read_framebuffer(g_px, W * H, &rw, &rh) > 0 && got > 0) {
            for (i = 0; i < got; i++) {
                if ((une_passe[i] & 0x00FFFFFFu) != (g_px[i] & 0x00FFFFFFu)) {
                    differents++;
                }
            }
        }
        say("\n-- repli a une TMU --\n");
        say("  pixels differents entre une passe et deux : %d sur %d\n",
            differents, got);
        check("le repli produit la meme image, verifiee par difference",
              got > 0 && differents * 100 < got);

        /* Le gain. Cent images de chaque cote : c'est la mesure que le ticket
           demande, et elle chiffre ce que le repli coute reellement plutot que
           de le supposer double. */
        dkr_glide_backend_force_single_tmu(0);
        t_une = GetTickCount();
        for (i = 0; i < 100; i++) {
            bk.begin_frame(bk.self, 0x000000);
            st.texture = h_b0; st.texture1 = h_b1;
            bk.set_state(bk.self, &st);
            dkr_glide_backend_chain(h_b0, h_b1, 4, 8);
            quad(&bk, W, H);
            bk.present(bk.self);
        }
        t_une = GetTickCount() - t_une;

        dkr_glide_backend_force_single_tmu(1);
        t_deux = GetTickCount();
        for (i = 0; i < 100; i++) {
            dkr_render_state st2;
            bk.begin_frame(bk.self, 0x000000);
            st.texture = h_b0; st.texture1 = 0;
            bk.set_state(bk.self, &st);
            quad(&bk, W, H);
            st2 = st; st2.blend = DKR_BLEND_ADDITIVE; st2.texture = h_b1;
            bk.set_state(bk.self, &st2);
            quad(&bk, W, H);
            bk.present(bk.self);
        }
        t_deux = GetTickCount() - t_deux;
        dkr_glide_backend_force_single_tmu(0);

        say("\n-- le gain --\n");
        say("  100 images en une passe  : %lu ms\n", t_une);
        say("  100 images en deux passes: %lu ms\n", t_deux);
        if (t_une) {
            say("  surcout du repli         : %lu %%\n",
                (t_deux * 100u) / t_une - 100u);
        }
    }

    bk.close(bk.self);
    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
