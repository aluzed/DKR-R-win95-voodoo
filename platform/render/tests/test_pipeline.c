/* E04 — la chaîne complète, sur une scène synthétique.
 *
 * Cinq modules s'emboîtent ici pour la première fois :
 *
 *     f3ddkr    lit la display list et valide
 *     transform applique la matrice modèle-vue-projection
 *     clip      découpe au plan proche, élimine les faces arrière
 *     backend   l'interface de E04-S01
 *     software  le rastériseur de référence, qui écrit l'image
 *
 * Ce qui est établi n'est pas que le rendu soit *juste* — il faudra le jeu pour
 * cela — mais que **la chaîne est continue** : une commande écrite en RDRAM
 * ressort en pixels, et chaque étage passe à son voisin ce que celui-ci attend.
 *
 * La scène est construite à la main dans une fausse RDRAM. C'est ce qui rend
 * l'épreuve possible sans ROM, et ce qui la rend concluante : on connaît la
 * réponse d'avance, donc on peut la vérifier au pixel plutôt qu'à l'œil.
 */
#include "render/f3ddkr.h"
#include "render/software.h"

#include <stdio.h>
#include <string.h>

static int g_fails;
static FILE *g_out;

static void check(const char *what, int condition)
{
    printf("  %s %s\n", condition ? "ok   " : "ECHEC", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", condition ? "ok   " : "ECHEC", what);
        fflush(g_out);
    }
    if (!condition) { g_fails++; }
}

static void report(const char *fmt, unsigned long a, unsigned long b)
{
    char line[160];
    sprintf(line, fmt, a, b);
    printf("%s\n", line);
    if (g_out) { fprintf(g_out, "%s\n", line); fflush(g_out); }
}

/* --- La fausse RDRAM -------------------------------------------------------- */
#define RAM 8192u
static unsigned char g_ram[RAM];

static void put32(unsigned int a, unsigned int v)
{
    g_ram[a] = (unsigned char)(v >> 24); g_ram[a + 1] = (unsigned char)(v >> 16);
    g_ram[a + 2] = (unsigned char)(v >> 8); g_ram[a + 3] = (unsigned char)v;
}
static void put16(unsigned int a, int v)
{
    g_ram[a] = (unsigned char)((unsigned)v >> 8); g_ram[a + 1] = (unsigned char)v;
}
static unsigned int cmd(unsigned int a, unsigned int w0, unsigned int w1)
{
    put32(a, w0); put32(a + 4, w1); return a + 8;
}

/* Une matrice au format N64 : seize parties entieres puis seize fractionnaires. */
static void put_matrix(unsigned int at, const float m[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            const int k = i * 4 + j;
            const int fixed = (int)(m[i][j] * 65536.0f);
            put16(at + (unsigned)k * 2,       (fixed >> 16) & 0xFFFF);
            put16(at + 32 + (unsigned)k * 2,   fixed        & 0xFFFF);
        }
    }
}

static unsigned pixel(int x, int y)
{
    int w, h;
    const unsigned *fb = dkr_software_framebuffer(&w, &h);
    if (!fb || x < 0 || y < 0 || x >= w || y >= h) { return 0; }
    return fb[(size_t)y * (size_t)w + (size_t)x];
}

int main(void)
{
    dkr_render_backend backend;
    dkr_f3d_context    ctx;
    unsigned int       at;

    const unsigned int MATRIX_AT   = 0x0400u;
    const unsigned int VERTEX_AT   = 0x0600u;
    const unsigned int TRIANGLE_AT = 0x0700u;

    g_out = fopen("D:\\PIPELINE.TXT", "w");

    dkr_render_backend_software(&backend);
    check("le rasteriseur s'ouvre en 320x240", backend.open(backend.self, 320, 240) != 0);
    backend.begin_frame(backend.self, 0x001030);   /* un bleu sombre reconnaissable */

    {
        dkr_render_state st;
        memset(&st, 0, sizeof(st));
        st.combine = DKR_COMBINE_SHADE;
        st.blend   = DKR_BLEND_OPAQUE;
        st.depth   = DKR_DEPTH_TEST_AND_WRITE;
        st.cull    = DKR_CULL_NONE;   /* le decodeur decide, pas l'etat */
        backend.set_state(backend.self, &st);
    }

    memset(g_ram, 0, sizeof(g_ram));

    /* --- La scene ----------------------------------------------------------- *
     *
     * Un quadrilatere de face, entre z = 200 et z = 400, plus un troisieme
     * triangle qui **traverse le plan proche** — c'est lui qui exerce le
     * decoupage, et sans lui la chaine ne serait eprouvee qu'a moitie. */
    {
        /* Une matrice identite : la projection fera tout le travail. */
        float ident[4][4] = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1} };
        put_matrix(MATRIX_AT, ident);
    }

    /* Six sommets. Les quatre premiers forment un carre devant la camera ; les
       deux derniers servent au triangle qui traverse. */
    {
        const short v[6][3] = {
            { -60, -60, 200 }, {  60, -60, 200 },
            {  60,  60, 200 }, { -60,  60, 200 },
            { -30,   0,  -50 },  /* derriere la camera */
            {  90,   0, 300 },
        };
        const unsigned char col[6][3] = {
            { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 },
            { 255, 0, 255 }, { 0, 255, 255 },
        };
        int i;
        for (i = 0; i < 6; i++) {
            const unsigned int a = VERTEX_AT + (unsigned)i * 10u;
            put16(a + 0, v[i][0]); put16(a + 2, v[i][1]); put16(a + 4, v[i][2]);
            g_ram[a + 6] = col[i][0]; g_ram[a + 7] = col[i][1];
            g_ram[a + 8] = col[i][2]; g_ram[a + 9] = 255;
        }
    }

    /* Trois triangles : deux pour le carre, un qui traverse le plan proche.
       Le bit 0x40 desactive la culling — la scene n'est pas orientee. */
    {
        const unsigned char tris[3][3] = { {0,1,2}, {0,2,3}, {4,5,1} };
        int i;
        for (i = 0; i < 3; i++) {
            const unsigned int a = TRIANGLE_AT + (unsigned)i * 16u;
            g_ram[a + 0] = 0x40;
            g_ram[a + 1] = tris[i][0];
            g_ram[a + 2] = tris[i][1];
            g_ram[a + 3] = tris[i][2];
        }
    }

    /* La display list. */
    at = 0;
    at = cmd(at, 0xBF000000u, 0x00000000u);                 /* DMAOffsets, bases nulles */
    at = cmd(at, 0x01000040u, MATRIX_AT);                   /* Matrix, emplacement 0 */
    at = cmd(at, 0x04000000u | (5u << 19) | (0u << 9), VERTEX_AT);   /* 6 sommets */
    at = cmd(at, 0x05200000u, TRIANGLE_AT);                 /* 3 triangles */
    (void)cmd(at, 0xB8000000u, 0u);                         /* fin */

    /* --- La traversee -------------------------------------------------------- */
    dkr_f3d_init(&ctx, g_ram, RAM, &backend);
    dkr_transform_set_viewport(&ctx.transform, 160.0f, -120.0f, 160.0f, 120.0f);
    {
        /* Une projection ou w = z, et z_clip = z/2 — donc z/w = 0,5.
         *
         * **Le facteur 0,5 n'est pas decoratif.** Avec z_clip = z, la division
         * donne z/w = 1,0 pour tout sommet, c'est-a-dire exactement la valeur a
         * laquelle le tampon de profondeur est efface. Le test `z < profondeur`
         * echoue alors partout et **rien n'est peint**, sans qu'aucun etage ne
         * signale quoi que ce soit.
         *
         * La premiere version de cette scene faisait cette erreur, et le
         * symptome — un ecran vide avec quatre triangles emis — est exactement
         * celui qu'on redoute en portage graphique : chaque etage se declare
         * satisfait. C'est le compteur `emitted` qui a permis de trancher, en
         * montrant que le probleme etait en aval du decodeur. */
        dkr_matrix proj;
        memset(&proj, 0, sizeof(proj));
        proj.m[0][0] = 1.0f; proj.m[1][1] = 1.0f; proj.m[2][2] = 0.5f;
        proj.m[2][3] = 1.0f;
        dkr_transform_set_projection(&ctx.transform, &proj);
    }

    check("la display list s'execute", dkr_f3d_run(&ctx, 0) == 5);
    check("les six sommets sont charges",  ctx.state.vertices  == 6);
    check("les trois triangles sont lus",  ctx.state.triangles == 3);

    report("  triangles demandes : %lu, emis : %lu",
           ctx.state.triangles, ctx.state.emitted);
    report("  decoupes en deux : %lu, ecartes : %lu",
           ctx.state.clip_split, ctx.state.clipped_away);

    /* Le triangle a cheval doit avoir ete decoupe : un sommet derriere donne un
       quadrilatere, donc deux triangles. C'est le controle qui prouve que le
       decoupage est bien **dans** la chaine et pas seulement dans sa suite
       d'epreuve. */
    check("le triangle a cheval a ete decoupe en deux", ctx.state.clip_split == 1);
    check("la chaine a emis plus de triangles qu'elle n'en a lu",
          ctx.state.emitted > ctx.state.triangles);
    check("aucun rejet de plage", ctx.state.rejects[DKR_F3D_REJECT_ADDRESS] == 0 &&
                                  ctx.state.rejects[DKR_F3D_REJECT_INDEX]   == 0);

    /* --- L'image ------------------------------------------------------------- *
     *
     * Le carre est centre et couvre de -60 a +60 en x, a z = 200. Avec une
     * echelle de 160 et w = z, il occupe 160 +/- 48 pixels. Le centre doit donc
     * etre peint, et un coin de l'ecran rester au fond. */
    check("le centre de l'ecran est peint",
          (pixel(160, 120) & 0x00FFFFFFu) != 0x001030u);
    check("un coin reste au fond",
          (pixel(4, 4) & 0x00FFFFFFu) == 0x001030u);
    /* Les couleurs des sommets sont interpolees : le centre du carre est un
       melange, donc aucune composante ne domine a 255. */
    {
        /* Le controle doit etre serre : une condition lache passerait sur la
           couleur de fond, ce qui ne prouverait rien. Le fond est 0x001030 ;
           un melange des sommets rouge, vert et bleu a forcement du rouge, que
           le fond n'a pas du tout. */
        const unsigned c = pixel(160, 120);
        const unsigned r = (c >> 16) & 0xFF;
        check("et sa couleur porte du rouge, que le fond n'a pas", r > 20);
    }

    check("l'image s'ecrit", dkr_software_write_bmp("D:\\PIPELINE.BMP") != 0);

    backend.close(backend.self);

    printf("\n%d echec(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d echec(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
