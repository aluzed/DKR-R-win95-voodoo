/* La scène synthétique, écrite une seule fois.
 *
 * Deux épreuves l'emploient : `test_pipeline.c`, qui vérifie que la chaîne est
 * continue, et `test_compare.c`, qui fait passer la *même* entrée dans le
 * rastériseur de référence et dans la Voodoo pour comparer les deux images.
 *
 * **C'est cette seconde épreuve qui impose de partager le code.** Un
 * comparateur dont les deux côtés construisent chacun leur scène ne mesure pas
 * une différence de rendu : il mesure d'abord la dérive entre deux copies. La
 * divergence apparaîtrait plus tard, sur une modification anodine de l'une des
 * deux, et serait attribuée au matériel.
 *
 * La scène est bâtie à la main dans une fausse RDRAM : on connaît la réponse
 * d'avance, donc on peut la vérifier au pixel plutôt qu'à l'œil, et l'épreuve
 * tient sans la ROM.
 */
#ifndef DKR_SCENE_SYNTHETIC_H
#define DKR_SCENE_SYNTHETIC_H

#include "render/f3ddkr.h"

#include <string.h>

#define DKR_SCENE_RAM 8192u

/* --- Écriture en RDRAM, gros-boutiste comme la vraie ------------------------ */

static void scene_put32(unsigned char *r, unsigned int a, unsigned int v)
{
    r[a] = (unsigned char)(v >> 24); r[a + 1] = (unsigned char)(v >> 16);
    r[a + 2] = (unsigned char)(v >> 8); r[a + 3] = (unsigned char)v;
}

static void scene_put16(unsigned char *r, unsigned int a, int v)
{
    r[a] = (unsigned char)((unsigned)v >> 8); r[a + 1] = (unsigned char)v;
}

static unsigned int scene_cmd(unsigned char *r, unsigned int a,
                              unsigned int w0, unsigned int w1)
{
    scene_put32(r, a, w0); scene_put32(r, a + 4, w1); return a + 8;
}

/* Une matrice au format N64 : seize parties entières puis seize fractionnaires,
   chacune sur seize bits — le 16.16 du microcode, coupé en deux moitiés. */
static void scene_put_matrix(unsigned char *r, unsigned int at,
                             const float m[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            const int k = i * 4 + j;
            const int fixed = (int)(m[i][j] * 65536.0f);
            scene_put16(r, at + (unsigned)k * 2,      (fixed >> 16) & 0xFFFF);
            scene_put16(r, at + 32 + (unsigned)k * 2,  fixed        & 0xFFFF);
        }
    }
}

/* --- La scène ---------------------------------------------------------------- *
 *
 * Un quadrilatère de face à z = 200, plus un troisième triangle qui **traverse
 * le plan proche** — c'est lui qui exerce le découpage, et sans lui la chaîne ne
 * serait éprouvée qu'à moitié. Il recouvre partiellement le quadrilatère, ce qui
 * met aussi le tampon de profondeur en jeu : c'est là que le rastériseur de
 * référence, qui trie sur z dans [0,1], et la carte, qui trie sur un tampon w,
 * peuvent le plus facilement diverger. */
static void scene_build(unsigned char *ram)
{
    const unsigned int MATRIX_AT   = 0x0400u;
    const unsigned int VERTEX_AT   = 0x0600u;
    const unsigned int TRIANGLE_AT = 0x0700u;
    unsigned int at;

    memset(ram, 0, DKR_SCENE_RAM);

    {
        /* Identité : c'est la projection qui fait tout le travail. */
        float ident[4][4] = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1} };
        scene_put_matrix(ram, MATRIX_AT, ident);
    }

    {
        const short v[6][3] = {
            { -60, -60, 200 }, {  60, -60, 200 },
            {  60,  60, 200 }, { -60,  60, 200 },
            { -30,   0,  -50 },   /* derrière la caméra */
            {  90,   0, 300 },
        };
        const unsigned char col[6][3] = {
            { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 },
            { 255, 0, 255 }, { 0, 255, 255 },
        };
        int i;
        for (i = 0; i < 6; i++) {
            const unsigned int a = VERTEX_AT + (unsigned)i * 10u;
            scene_put16(ram, a + 0, v[i][0]);
            scene_put16(ram, a + 2, v[i][1]);
            scene_put16(ram, a + 4, v[i][2]);
            ram[a + 6] = col[i][0]; ram[a + 7] = col[i][1];
            ram[a + 8] = col[i][2]; ram[a + 9] = 255;
        }
    }

    {
        /* Le bit 0x40 désactive l'élimination : la scène n'est pas orientée. */
        const unsigned char tris[3][3] = { {0,1,2}, {0,2,3}, {4,5,1} };
        int i;
        for (i = 0; i < 3; i++) {
            const unsigned int a = TRIANGLE_AT + (unsigned)i * 16u;
            ram[a + 0] = 0x40;
            ram[a + 1] = tris[i][0];
            ram[a + 2] = tris[i][1];
            ram[a + 3] = tris[i][2];
        }
    }

    at = 0;
    at = scene_cmd(ram, at, 0xBF000000u, 0x00000000u);   /* DMAOffsets, bases nulles */
    at = scene_cmd(ram, at, 0x01000040u, MATRIX_AT);     /* Matrix, emplacement 0 */
    at = scene_cmd(ram, at, 0x04000000u | (5u << 19) | (0u << 9), VERTEX_AT);
    at = scene_cmd(ram, at, 0x05200000u, TRIANGLE_AT);   /* 3 triangles */
    (void)scene_cmd(ram, at, 0xB8000000u, 0u);           /* fin */
}

/* Prépare le contexte et la projection pour une fenêtre de `w` x `h`.
 *
 * La projection est telle que `w_clip = z` et `z_clip = z / 2`, donc `z/w` vaut
 * 0,5. **Le facteur 0,5 n'est pas décoratif** : avec `z_clip = z`, la division
 * donne 1,0 partout, c'est-à-dire exactement la valeur d'effacement du tampon de
 * profondeur. Le test échoue alors sur tout l'écran et **rien n'est peint**,
 * sans qu'aucun étage ne signale quoi que ce soit — chaque module se déclare
 * satisfait. C'est le compteur `emitted` qui avait permis de trancher. */
static void scene_setup(dkr_f3d_context *ctx, unsigned char *ram,
                        dkr_render_backend *bk, int w, int h)
{
    dkr_matrix proj;

    dkr_f3d_init(ctx, ram, DKR_SCENE_RAM, bk);
    dkr_transform_set_viewport(&ctx->transform,
                               (float)w * 0.5f, -(float)h * 0.5f,
                               (float)w * 0.5f,  (float)h * 0.5f);

    memset(&proj, 0, sizeof(proj));
    proj.m[0][0] = 1.0f; proj.m[1][1] = 1.0f; proj.m[2][2] = 0.5f;
    proj.m[2][3] = 1.0f;
    /* **Un terme constant sur z, sans quoi la scene ne teste pas la profondeur.**
     *
     * Avec `z_clip = 0,5 z` et `w = z`, le rapport `z/w` vaut 0,5 pour *tout*
     * sommet : le quadrilatere et le triangle decoupe se retrouvent exactement
     * a la meme profondeur, et leur recouvrement produit un conflit de tri.
     * Le rasteriseur logiciel y repondait par un pointille, la carte par un bord
     * net — deux reponses egalement arbitraires a une question mal posee, et la
     * comparaison mesurait cette ambiguite plutot que le rendu.
     *
     * Le terme rend `z/w = 0,5 - 20/z`, qui varie avec la distance. La scene
     * departage alors reellement les deux surfaces. */
    proj.m[3][2] = -20.0f;
    dkr_transform_set_projection(&ctx->transform, &proj);
}

/* L'état de rendu commun. Sans texture ni brouillard : ce que la comparaison
   mesure ici est la géométrie, la couleur interpolée et la profondeur — les
   trois choses que les deux backends savent déjà faire. */
static void scene_state(dkr_render_state *st)
{
    memset(st, 0, sizeof(*st));
    st->combine = DKR_COMBINE_SHADE;
    st->blend   = DKR_BLEND_OPAQUE;
    st->depth   = DKR_DEPTH_TEST_AND_WRITE;
    st->cull    = DKR_CULL_NONE;   /* le décodeur décide, pas l'état */
}

#endif /* DKR_SCENE_SYNTHETIC_H */
