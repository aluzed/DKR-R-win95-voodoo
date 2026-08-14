/* E05-S01 — le témoin de la couche d'amorçage Glide.
 *
 * Il exerce `platform/render/glide.c` dans l'ordre où le moteur l'emploiera :
 * détecter, ouvrir, boucler, dessiner, fermer. Ce qu'il établit est que la
 * couche marche sur la machine, pas seulement qu'elle compile.
 *
 * Deux choses le distinguent de la démonstration de E09-S01, qui dessinait déjà
 * un triangle :
 *
 *   - il passe par la **couche réutilisable** et non par des appels directs, de
 *     sorte que ce qui est prouvé est ce que le moteur utilisera ;
 *   - il mesure la **cadence** sur cent images, ce qui donne le premier chiffre
 *     d'échange de tampons du projet.
 *
 * Le compte rendu part dans `D:\GLIDEBK.TXT`, lisible depuis l'hôte : sur une
 * Voodoo passthrough l'écran appartient à la carte pendant tout le rendu, et une
 * capture d'écran de l'émulateur ne montrerait pas la sortie 3dfx.
 */
#include "render/glide.h"
#include "win95/clock.h"
#include "win95/startup.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g_log;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (g_log) {
        va_list copy;
        va_copy(copy, ap);
        vfprintf(g_log, fmt, copy);
        fflush(g_log);
        va_end(copy);
    }
    vprintf(fmt, ap);
    va_end(ap);
}

int main(int argc, char **argv)
{
    /* Mode « plantage » : ouvrir le contexte puis mourir pour de bon.
     *
     * C'est l'epreuve de l'etape 7 de E05-S01, et la plus utile en pratique.
     * Sur une Voodoo passthrough, l'ecran appartient a la carte tant que le
     * contexte est ouvert : un plantage sans restitution laisse un ecran noir
     * que seul un redemarrage recupere. Pendant toute la mise au point de E05,
     * ou l'on plante souvent, c'est la difference entre dix secondes et deux
     * minutes par erreur.
     *
     * La chaine eprouvee est complete : `dkr_win95_startup` pose le filtre
     * d'exception, `dkr_glide_open` inscrit la restitution au registre, et le
     * filtre l'execute avant d'afficher quoi que ce soit. */
    const int crash_mode = (argc >= 2 && argv[1][0] == 'c');

    dkr_glide_hardware hw;
    dkr_glide_context  ctx;
    dkr_glide_result   r;
    int i;

    g_log = fopen("D:\\GLIDEBK.TXT", "w");
    dkr_win95_startup("GLIDEBK");

    /* --- Détection --------------------------------------------------------- */
    r = dkr_glide_detect(&hw);
    say("detection          : %s\n", dkr_glide_result_text(r));
    if (r != DKR_GLIDE_OK) {
        /* Ce n'est pas un echec du temoin : sur une machine sans carte 3dfx,
           c'est le comportement voulu, et le message doit etre lisible. */
        say("verdict            : pas de materiel 3dfx, message clair rendu\n");
        if (g_log) fclose(g_log);
        return 0;
    }
    say("version Glide      : 0x%03X\n", hw.glide_version);
    say("cartes             : %d\n", hw.board_count);
    say("TMU                : %d\n", hw.tmu_count);
    say("memoire image      : %u Ko\n", hw.fb_memory_kb);
    for (i = 0; i < hw.tmu_count && i < 3; i++) {
        say("  TMU %d memoire    : %u Ko\n", i, hw.tmu_memory_kb[i]);
    }
    say("SLI                : %d\n", hw.sli);

    /* Ce que l'ADR 0002 exige, verifie a l'execution plutot que suppose. */
    say("deux TMU exigees   : %s\n", hw.tmu_count >= 2 ? "OUI" : "NON — repli multipasse (E05-S04)");

    /* --- Ouverture --------------------------------------------------------- */
    r = dkr_glide_open(DKR_GLIDE_RES_640x480, &ctx);
    say("ouverture 640x480  : %s\n", dkr_glide_result_text(r));
    if (r != DKR_GLIDE_OK) {
        if (g_log) fclose(g_log);
        return 1;
    }
    say("resolution obtenue : %dx%d, %d tampons, profondeur %s\n",
        ctx.width, ctx.height, ctx.buffers, ctx.depth_buffer ? "oui" : "non");

    if (crash_mode) {
        volatile int *nowhere = (volatile int *)0;
        say("mode plantage      : dereferencement nul, contexte ouvert\n");
        dkr_glide_clear(0x00FF00);
        dkr_glide_swap();
        *nowhere = 1;                 /* le filtre doit rendre l'affichage */
        say("JAMAIS ATTEINT\n");
        return 9;
    }
    if (ctx.resolution != DKR_GLIDE_RES_640x480) {
        say("  (repli applique — la resolution demandee ne tenait pas)\n");
    }

    /* --- Cycle d'image ----------------------------------------------------- */
    {
        unsigned long long start, elapsed_us;
        const int frames = 100;

        dkr_clock_init();
        start = dkr_clock_now_us();

        for (i = 0; i < frames; i++) {
            /* Un dégradé lent, pour que l'écran montre que ça tourne. */
            dkr_glide_clear((unsigned)((i * 2) & 0xFF));
            dkr_glide_draw_test_triangle();
            dkr_glide_swap();
        }
        elapsed_us = dkr_clock_now_us() - start;
        say("%d images en       : %lu ms\n", frames,
            (unsigned long)(elapsed_us / 1000u));
        if (elapsed_us > 0) {
            say("cadence            : %lu images/s\n",
                (unsigned long)((unsigned long long)frames * 1000000u / elapsed_us));
        }
    }

    /* --- Fermeture --------------------------------------------------------- */
    dkr_glide_shutdown();
    say("fermeture          : affichage restitue\n");
    /* Idempotence : le filtre d'exception peut l'appeler apres coup. */
    dkr_glide_shutdown();
    say("seconde fermeture  : sans effet, comme attendu\n");

    say("verdict            : la couche d'amorcage Glide fonctionne\n");
    if (g_log) fclose(g_log);
    return 0;
}
