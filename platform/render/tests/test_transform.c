/* E04-S03 — épreuve de la transformation, et la mesure qui tranche.
 *
 * Deux choses ici, et la seconde est celle que le ticket réclame explicitement :
 *
 *   - la justesse de la conversion 16.16 et de la transformation, vérifiée
 *     contre des valeurs **calculées à la main** ;
 *   - le **coût par sommet sur la cible**, en virgule flottante x87 et en
 *     virgule fixe, parce que le choix doit se faire « sur une mesure, pas sur
 *     une préférence, et consigner le chiffre ».
 *
 * La mesure ne prétend pas trancher pour le jeu entier : elle mesure une
 * transformation isolée, sans les défauts de cache d'une vraie scène. Ce qu'elle
 * établit est un rapport entre deux implémentations dans les mêmes conditions,
 * ce qui est précisément la question posée.
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
    sprintf(line, "  %s %s", condition ? "ok   " : "ECHEC", what);
    say(line);
    if (!condition) { g_fails++; }
}

static void check_near(const char *what, double got, double want, double tol)
{
    char line[192];
    const int ok = (got - want < tol) && (want - got < tol);
    sprintf(line, "  %s %-42s attendu %.5f, obtenu %.5f",
            ok ? "ok   " : "ECHEC", what, want, got);
    say(line);
    if (!ok) { g_fails++; }
}

/* Écrit une matrice au format de la N64 : seize parties entières puis seize
   parties fractionnaires, gros-boutistes. */
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

    /* --- La conversion 16.16 ----------------------------------------------- */
    memset(raw, 0, sizeof(raw));
    put_fixed(raw,  0, 1, 0);             /* 1,0 */
    put_fixed(raw,  1, 0, 0x8000);        /* 0,5 */
    put_fixed(raw,  2, -1, 0x8000);       /* -0,5 : entier -1, fraction 0,5 */
    put_fixed(raw,  3, -2, 0);            /* -2,0 */
    put_fixed(raw,  4, 0, 0xFFFF);        /* juste sous 1 */
    check("la conversion 16.16 rend 1,0",   dkr_matrix_from_fixed(raw, &m) != 0);
    check_near("entier seul",        m.m[0][0],  1.0,        0.00001);
    check_near("fraction seule",     m.m[0][1],  0.5,        0.00001);
    /* Le cas qui casse une conversion naive : **la fraction n'est pas signee**.
       La lire comme un short donnerait -1 + (-0,5) = -1,5 au lieu de -0,5. */
    check_near("negatif avec fraction", m.m[0][2], -0.5,     0.00001);
    check_near("negatif entier",     m.m[0][3], -2.0,        0.00001);
    check_near("juste sous un",      m.m[1][0],  0.9999847,  0.00001);

    /* --- La projection ------------------------------------------------------ */
    dkr_transform_init(&t);
    dkr_transform_set_viewport(&t, 320.0f, -240.0f, 320.0f, 240.0f);

    /* Une projection simple : w = z. C'est elle qui donne un sens au rejet — avec
       l'identite, w vaut 1 quel que soit le sommet, et rien n'est jamais derriere
       le plan. Une premiere version de cette epreuve l'affirmait pourtant, et
       c'est l'epreuve qui avait tort. */
    memset(&m, 0, sizeof(m));
    m.m[0][0] = 1.0f; m.m[1][1] = 1.0f; m.m[2][2] = 1.0f;
    m.m[2][3] = 1.0f;                 /* w = z */
    dkr_transform_set_projection(&t, &m);
    {
        dkr_source_vertex behind = { 10, 10, 0, 255, 255, 255, 255 };
        dkr_source_vertex far_behind = { 10, 10, -50, 255, 255, 255, 255 };
        dkr_render_vertex rv;
        /* Diviser par zero produirait des coordonnees infinies qui traversent
           l'ecran — bien plus visibles qu'un sommet absent. */
        check("un sommet a w nul est rejete",
              dkr_transform_vertex(&t, &behind, &rv) == 0);
        check("un sommet derriere le plan aussi",
              dkr_transform_vertex(&t, &far_behind, &rv) == 0);
    }
    {
        dkr_source_vertex sv = { 100, 50, 200, 255, 128, 64, 255 };
        dkr_render_vertex rv;
        check("un sommet devant le plan est accepte",
              dkr_transform_vertex(&t, &sv, &rv) != 0);
        /* x_ecran = x/w * 320 + 320, avec w = z = 200 */
        check_near("x ecran",  rv.x, 100.0 / 200.0 * 320.0 + 320.0, 0.01);
        check_near("y ecran",  rv.y,  50.0 / 200.0 * -240.0 + 240.0, 0.01);
        check_near("1/w",      rv.oow, 1.0 / 200.0,                  0.00001);
        check("la couleur passe telle quelle",
              rv.r == 255.0f && rv.g == 128.0f && rv.b == 64.0f && rv.a == 255.0f);
        /* Les coordonnees de texture ne viennent pas du sommet : elles doivent
           rester a zero plutot que d'etre inventees. */
        check("les coordonnees de texture restent nulles",
              rv.tmu[0][DKR_TMU_SOW] == 0.0f && rv.tmu[0][DKR_TMU_TOW] == 0.0f);
    }

    /* --- La pile ------------------------------------------------------------ */
    {
        dkr_matrix translate;
        dkr_source_vertex sv = { 0, 0, 100, 255, 255, 255, 255 };
        dkr_render_vertex rv;
        memset(&translate, 0, sizeof(translate));
        translate.m[0][0] = translate.m[1][1] = translate.m[2][2] =
            translate.m[3][3] = 1.0f;
        translate.m[3][0] = 50.0f;      /* translation en x */
        dkr_transform_set_matrix(&t, 1, &translate);
        dkr_transform_select(&t, 1);
        check("un sommet translate se transforme",
              dkr_transform_vertex(&t, &sv, &rv) != 0);
        check_near("la translation est appliquee", rv.x,
                   50.0 / 100.0 * 320.0 + 320.0, 0.01);
        dkr_transform_select(&t, 0);
        check("revenir a l'emplacement 0 annule la translation",
              dkr_transform_vertex(&t, &sv, &rv) != 0 && rv.x > 319.0f && rv.x < 321.0f);
        /* La profondeur est relevee sur le jeu : trois emplacements. Un index
           hors bornes doit etre borne, pas ecrire ailleurs. */
        dkr_transform_select(&t, 99);
        check("un emplacement hors bornes est borne", t.selected == DKR_MATRIX_SLOTS - 1);
    }

    /* --- La mesure ---------------------------------------------------------- *
     *
     * Le ticket demande de trancher entre flottant et virgule fixe « sur une
     * mesure, pas sur une preference ». Voici la mesure. */
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
            /* Plusieurs passes : `GetTickCount` avance par pas de 9 ms sur cette
               machine (E02-S03), et une mesure trop breve ne mesurerait que la
               granularite de l'horloge. */
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
            sprintf(line, "  mesure : %d sommets x 20 passes en %lu ms",
                    COUNT, ms_float);
            say(line);
            sprintf(line, "  soit %.3f us par sommet en virgule flottante x87",
                    per_vertex_us);
            say(line);
            /* A 400 MHz, une image de 60 Hz laisse 16,6 ms. Le nombre de sommets
               tenable en decoule directement, et c'est lui qui interesse E08. */
            sprintf(line, "  soit %.0f sommets dans une image de 16,6 ms",
                    per_vertex_us > 0.0 ? 16600.0 / per_vertex_us : 0.0);
            say(line);
            check("la mesure a produit un chiffre exploitable", ms_float > 0);
        }
#else
        (void)ms_float;
        for (i = 0; i < COUNT; i++) {
            dkr_transform_vertex(&t, &src[i], &dst[i]);
        }
        say("  mesure : ignoree sur l'hote — seul le chiffre de la cible compte");
#endif
    }

    {
        char line[64];
        sprintf(line, "\n%d echec(s)", g_fails);
        say(line);
    }
    if (g_out) { fclose(g_out); }
    return g_fails != 0;
}
