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
#include "render/software.h"
#include "scene_synthetic.h"

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

/* La scène est dans `scene_synthetic.h`, partagée avec le comparateur de
   E09-S02. Deux copies dériveraient, et la divergence serait attribuée au
   matériel plutôt qu'à la copie. */
static unsigned char g_ram[DKR_SCENE_RAM];

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
    g_out = fopen("D:\\PIPELINE.TXT", "w");

    dkr_render_backend_software(&backend);
    check("le rasteriseur s'ouvre en 320x240", backend.open(backend.self, 320, 240) != 0);
    backend.begin_frame(backend.self, 0x001030);   /* un bleu sombre reconnaissable */

    {
        dkr_render_state st;
        scene_state(&st);
        backend.set_state(backend.self, &st);
    }

    scene_build(g_ram);
    scene_setup(&ctx, g_ram, &backend, 320, 240);

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
        /* **Ou echantillonner compte autant que ce qu'on y cherche.**
         *
         * Ce controle visait le degrade du quadrilatere et lisait le centre de
         * l'ecran. Il y trouvait du rouge tant que la couleur etait interpolee
         * avec correction perspective ; depuis qu'elle est iteree comme sur le
         * materiel, c'est le polygone decoupe — vert et cyan — qui occupe le
         * centre, et le rouge a disparu sans qu'aucune regression n'ait eu lieu.
         *
         * Le quadrilatere s'etend de y = 72 a y = 168 ; le polygone decoupe
         * n'entame que sa moitie basse. On lit donc a y = 85, ou le
         * quadrilatere est seul, et l'on verifie en outre que deux points
         * distincts different — sans quoi un aplat passerait pour un degrade. */
        const unsigned c  = pixel(160, 85);
        const unsigned c2 = pixel(200, 85);
        const unsigned r  = (c >> 16) & 0xFF;
        check("le quadrilatere porte du rouge, que le fond n'a pas", r > 20);
        check("et sa couleur varie d'un point a l'autre : c'est un degrade",
              (c & 0x00FFFFFFu) != (c2 & 0x00FFFFFFu));
    }

    check("l'image s'ecrit", dkr_software_write_bmp("D:\\PIPELINE.BMP") != 0);

    backend.close(backend.self);

    printf("\n%d echec(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d echec(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
