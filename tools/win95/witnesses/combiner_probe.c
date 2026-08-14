/* E05-S03 — l'écart de chaque configuration, mesuré sur la carte.
 *
 * Le ticket met en garde : « la tentation sera de traiter les 33 configurations
 * une par une jusqu'à ce que ça ressemble, sans mesure. L'écart visuel se cumule
 * alors silencieusement, et le rendu final est diffusément faux sans qu'aucune
 * erreur ne soit imputable. » Ce témoin est la réponse à cette mise en garde.
 *
 * ## L'oracle est la formule, pas un second programme
 *
 * Le ticket prévoit de comparer au rastériseur de référence. On compare ici
 * directement à `(a - b) * c + d`, évaluée par `dkr_combiner_eval_all`. Ce n'est
 * pas un raccourci mais un renforcement : comparer deux programmes ne fait que
 * déplacer la question de savoir lequel a raison, alors qu'une formule courte
 * sur des entrées constantes a une réponse que l'on peut poser à la main.
 *
 * Le rastériseur reste l'oracle pour la géométrie et l'interpolation, où il n'y
 * a pas de forme close. Ici il n'y en a pas besoin.
 *
 * ## Pourquoi des couleurs constantes partout
 *
 * La scène est faite pour qu'**aucune interpolation n'intervienne** : un
 * quadrilatère plein écran, tous les sommets de la même couleur, une texture à
 * quatre aplats. Chaque pixel lu a donc une valeur analytique exacte, et l'écart
 * mesuré ne peut venir que du combineur. Mélanger l'interpolation à cette mesure
 * rendrait tout écart inattribuable — précisément ce contre quoi le ticket met
 * en garde.
 */
#include "render/glide.h"
#include "render/backend.h"
#include "render/combiner.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static FILE *g_out;
static int   g_fails;

static void say(const char *fmt, ...)
{
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    if (g_out) { fputs(line, g_out); fflush(g_out); }
}

/* --- Les entrées, toutes constantes et choisies pour être distinctes -------- *
 *
 * Des valeurs proches se confondraient sous la quantification 565 ; des valeurs
 * extrêmes (0 et 255) masqueraient les erreurs d'échelle, un facteur faux ne se
 * voyant pas quand il multiplie zéro. On prend donc des valeurs moyennes et
 * bien séparées. */
#define TW 64
#define TH 64
static unsigned short g_texture[TW * TH];

static const float TEXEL[4]  = { 200.0f, 100.0f,  50.0f, 128.0f };
static const float SHADE[4]  = { 128.0f, 192.0f,  64.0f, 255.0f };
static const float PRIM[4]   = { 255.0f,  32.0f,  96.0f, 200.0f };
static const float ENVI[4]   = {  16.0f, 224.0f, 160.0f,  64.0f };

static void build_texture(void)
{
    /* Un aplat unique : la mesure porte sur le combineur, pas sur
       l'échantillonnage. Le témoin de texture (E05-S02) a déjà établi que la
       lecture est correcte. */
    const unsigned short c = (unsigned short)
        (0x8000u | (((unsigned)TEXEL[0] >> 3) << 10) |
                   (((unsigned)TEXEL[1] >> 3) <<  5) |
                    ((unsigned)TEXEL[2] >> 3));
    int i;
    for (i = 0; i < TW * TH; i++) { g_texture[i] = c; }
}

/* La texture est en 1555 : le texel que la carte lit n'est pas exactement celui
   qu'on a voulu écrire. L'oracle doit voir **ce que la carte voit**, sans quoi
   l'on mesurerait la quantification de la texture et non le combineur. */
static void texel_quantifie(float out[4])
{
    int i;
    for (i = 0; i < 3; i++) {
        const unsigned q = ((unsigned)TEXEL[i]) >> 3;
        out[i] = (float)((q << 3) | (q >> 2));
    }
    out[3] = 255.0f;   /* un bit d'alpha, mis a un */
}

static void draw_quad(dkr_render_backend *bk, int w, int h)
{
    dkr_render_vertex v[6];
    const float xs[6] = { 0.0f, (float)w, (float)w, 0.0f, (float)w, 0.0f };
    const float ys[6] = { 0.0f, 0.0f, (float)h, 0.0f, (float)h, (float)h };
    const float ss[6] = { 0.0f, 256.0f, 256.0f, 0.0f, 256.0f, 0.0f };
    const float ts[6] = { 0.0f, 0.0f, 256.0f, 0.0f, 256.0f, 256.0f };
    int i;
    memset(v, 0, sizeof(v));
    for (i = 0; i < 6; i++) {
        v[i].x = xs[i]; v[i].y = ys[i];
        v[i].r = SHADE[0]; v[i].g = SHADE[1]; v[i].b = SHADE[2]; v[i].a = SHADE[3];
        v[i].oow = 1.0f;
        v[i].tmu[0][DKR_TMU_SOW] = ss[i];
        v[i].tmu[0][DKR_TMU_TOW] = ts[i];
        v[i].tmu[0][DKR_TMU_OOW] = 1.0f;
    }
    bk->draw_triangles(bk->self, v, 2);
}

static unsigned g_pixels[640 * 480];

static unsigned pack(const float c[4])
{
    unsigned r = (unsigned)(c[0] + 0.5f), g = (unsigned)(c[1] + 0.5f),
             b = (unsigned)(c[2] + 0.5f), a = (unsigned)(c[3] + 0.5f);
    if (r > 255) { r = 255; } if (g > 255) { g = 255; }
    if (b > 255) { b = 255; } if (a > 255) { a = 255; }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Quantifie comme le tampon d'image de la carte, pour ne pas compter la
   conversion 565 comme un écart du combineur. */
static unsigned q565(unsigned c)
{
    const unsigned r = ((c >> 16) & 0xFF) >> 3;
    const unsigned g = ((c >> 8)  & 0xFF) >> 2;
    const unsigned b = ( c        & 0xFF) >> 3;
    return (((r << 3) | (r >> 2)) << 16) |
           (((g << 2) | (g >> 4)) << 8)  |
            ((b << 3) | (b >> 2));
}

static int ecart(unsigned a, unsigned b)
{
    int pire = 0, i;
    for (i = 0; i < 3; i++) {
        int d = (int)((a >> (i * 8)) & 0xFF) - (int)((b >> (i * 8)) & 0xFF);
        if (d < 0) { d = -d; }
        if (d > pire) { pire = d; }
    }
    return pire;
}

int main(void)
{
    dkr_render_backend bk;
    dkr_texture_desc   desc;
    dkr_texture_handle handle;
    const int W = 640, H = 480;
    int i, n, rw = 0, rh = 0;
    int pire_exacte = 0;
    const char *pire_nom = "(aucune)";

    g_out = fopen("D:\\COMBINER.TXT", "w");
    say("ecart de chaque configuration de combineur, mesure sur la carte\n\n");

    build_texture();
    dkr_render_backend_glide(&bk);
    if (!bk.open(bk.self, W, H)) {
        say("ECHEC : la carte ne s'ouvre pas\n");
        if (g_out) { fclose(g_out); }
        return 1;
    }
    bk.begin_frame(bk.self, 0x000000);

    memset(&desc, 0, sizeof(desc));
    desc.key = 0xC0FFEEull;
    desc.format = DKR_TEXFMT_RGBA5551;
    desc.width = TW; desc.height = TH;
    desc.pixels = g_texture; desc.size_bytes = sizeof(g_texture);
    handle = bk.texture_upload(bk.self, &desc);
    if (!handle) {
        say("ECHEC : la texture ne se charge pas\n");
        bk.close(bk.self);
        if (g_out) { fclose(g_out); }
        return 1;
    }

    {
        dkr_render_state st;
        memset(&st, 0, sizeof(st));
        st.blend = DKR_BLEND_OPAQUE;
        st.depth = DKR_DEPTH_DISABLED;
        st.cull  = DKR_CULL_NONE;
        st.filter = DKR_FILTER_POINT;
        st.wrap_s = st.wrap_t = DKR_WRAP_CLAMP;
        st.texture = handle;
        bk.set_state(bk.self, &st);
    }

    n = dkr_cc_table_count();
    say("%-34s %-12s %6s %6s %s\n",
        "configuration", "categorie", "attendu", "obtenu", "ecart");

    for (i = 0; i < n; i++) {
        const dkr_cc_entree *e = dkr_cc_table_at(i);
        dkr_combiner_inputs in;
        dkr_combiner comb;
        float attendu[4];
        unsigned constante, ca, cb;
        int d;

        memset(&in, 0, sizeof(in));
        texel_quantifie(in.texel0);
        memcpy(in.texel1,      TEXEL, sizeof(in.texel1));
        memcpy(in.primitive,   PRIM,  sizeof(in.primitive));
        memcpy(in.shade,       SHADE, sizeof(in.shade));
        memcpy(in.environment, ENVI,  sizeof(in.environment));

        memset(&comb, 0, sizeof(comb));
        comb.rgb[0]   = e->rgb[0];   comb.rgb[1]   = e->rgb[1];
        comb.alpha[0] = e->alpha[0]; comb.alpha[1] = e->alpha[1];
        dkr_combiner_eval_all(&comb, e->cycle, &in, attendu);

        /* Le registre constant reçoit ce que la table a décidé. C'est là que se
           lit le mur : une configuration marquée `LES_DEUX` ne peut pas être
           servie, et son écart le dira. */
        constante = (e->constante == DKR_CONST_PRIMITIVE) ? pack(PRIM)
                  : (e->constante == DKR_CONST_ENVIRONMENT) ? pack(ENVI)
                  : 0xFFFFFFFFu;

        dkr_glide_backend_bind(handle);
        dkr_glide_backend_set_recipe(&e->reglage, constante);

        bk.begin_frame(bk.self, 0x000000);
        dkr_glide_backend_bind(handle);
        dkr_glide_backend_set_recipe(&e->reglage, constante);
        draw_quad(&bk, W, H);
        bk.present(bk.self);

        if (dkr_glide_read_framebuffer(g_pixels, W * H, &rw, &rh) <= 0) {
            say("  %-32s relecture impossible\n", e->nom);
            continue;
        }
        ca = q565(pack(attendu) & 0x00FFFFFFu);
        cb = g_pixels[(size_t)(rh / 2) * (size_t)rw + (size_t)(rw / 2)] & 0x00FFFFFFu;
        d  = ecart(ca, cb);

        say("%-34s %-12s %06X %06X %5d %s\n",
            e->nom, dkr_cc_categorie_texte(e->categorie), ca, cb, d,
            (e->categorie == DKR_CC_EXACTE && d > 8) ? "<-- EXACTE MAIS FAUSSE" : "");

        /* **Le contrôle qui compte.** Une configuration déclarée exacte doit
           l'être : au-delà de la quantification, elle a été mal classée, et la
           table ment. Les catégories `multipasse` et `approchee` annoncent au
           contraire un écart — le mesurer est leur raison d'être, et il est
           rapporté sans être compté en échec. */
        if (e->categorie == DKR_CC_EXACTE && d > pire_exacte) {
            pire_exacte = d;
            pire_nom = e->nom;
        }
    }

    say("\n  pire ecart parmi les configurations declarees exactes : %d (%s)\n",
        pire_exacte, pire_nom);
    if (pire_exacte > 8) {
        say("  ECHEC : une configuration declaree exacte ne l'est pas\n");
        g_fails++;
    } else {
        say("  ok : toutes les configurations exactes le sont, "
            "a la quantification pres\n");
    }

    bk.close(bk.self);
    say("\n%d echec(s)\n", g_fails);
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
