/* E04-S05 — mise en œuvre. Le contrat est dans `clip.h`. */
#include "clip.h"

#include <string.h>

/* Interpole **tous** les attributs entre deux sommets.
 *
 * Écrite une seule fois et employée pour chaque sommet produit : c'est ce qui
 * empêche d'oublier un attribut dans un cas particulier. L'oubli ne se verrait
 * que sur les triangles découpés — donc rarement, donc tard. */
static void lerp_vertex(const dkr_clip_vertex *a, const dkr_clip_vertex *b,
                        float k, dkr_clip_vertex *out)
{
    out->x = a->x + (b->x - a->x) * k;
    out->y = a->y + (b->y - a->y) * k;
    out->z = a->z + (b->z - a->z) * k;
    out->w = a->w + (b->w - a->w) * k;
    out->r = a->r + (b->r - a->r) * k;
    out->g = a->g + (b->g - a->g) * k;
    out->b = a->b + (b->b - a->b) * k;
    out->a = a->a + (b->a - a->a) * k;
    out->s = a->s + (b->s - a->s) * k;
    out->t = a->t + (b->t - a->t) * k;
}

/* La distance signée d'un sommet à un plan, en espace homogène.
 *
 * Cinq plans : le plan proche, et les quatre côtés de la bande de garde. Les
 * écrire comme des fonctions linéaires de (x, y, w) permet de les traiter par la
 * même boucle — donc de n'avoir qu'un seul découpeur à relire. */
static float plane_distance(const dkr_clip_vertex *v, int plane)
{
    const float g = DKR_CLIP_GUARD;
    switch (plane) {
    case 0:  return v->w - DKR_CLIP_NEAR_EPSILON;   /* plan proche */
    case 1:  return v->x + g * v->w;                /* garde gauche */
    case 2:  return g * v->w - v->x;                /* garde droite */
    case 3:  return v->y + g * v->w;                /* garde haute */
    default: return g * v->w - v->y;                /* garde basse */
    }
}

#define CLIP_PLANES 5
/* Cinq plans peuvent porter un triangle à huit sommets : chacun en ajoute au
   plus un. La borne est atteignable, et la dépasser écraserait la pile. */
#define CLIP_MAX_VERTICES 8

int dkr_clip_near(const dkr_clip_vertex in[3], dkr_clip_vertex out[6])
{
    dkr_clip_vertex poly[CLIP_MAX_VERTICES];
    dkr_clip_vertex work[CLIP_MAX_VERTICES];
    int n = 3, plane, i, triangles;

    if (!in || !out) {
        return 0;
    }

    /* **Court-circuit.** La quasi-totalité des triangles est entièrement dans la
       bande, et sort d'ici sans qu'aucune arête ne soit calculée. C'est ce qui
       rend le découpage à cinq plans abordable là où le découpage complet ne le
       serait pas. */
    {
        int all_inside = 1;
        for (plane = 0; plane < CLIP_PLANES && all_inside; plane++) {
            for (i = 0; i < 3; i++) {
                if (plane_distance(&in[i], plane) < 0.0f) {
                    all_inside = 0;
                    break;
                }
            }
        }
        if (all_inside) {
            out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
            return 1;
        }
    }

    poly[0] = in[0]; poly[1] = in[1]; poly[2] = in[2];

    /* Sutherland-Hodgman, un plan après l'autre : pour chaque arête, on garde le
       sommet s'il est du bon côté, et l'on ajoute l'intersection si l'arête
       traverse. */
    for (plane = 0; plane < CLIP_PLANES; plane++) {
        int m = 0;
        for (i = 0; i < n; i++) {
            const dkr_clip_vertex *cur  = &poly[i];
            const dkr_clip_vertex *next = &poly[(i + 1) % n];
            const float dc = plane_distance(cur,  plane);
            const float dn = plane_distance(next, plane);

            if (dc >= 0.0f && m < CLIP_MAX_VERTICES) {
                work[m++] = *cur;
            }
            if ((dc >= 0.0f) != (dn >= 0.0f) && m < CLIP_MAX_VERTICES) {
                /* Le dénominateur ne peut pas s'annuler : les deux sommets sont
                   de part et d'autre, donc leurs distances diffèrent. */
                lerp_vertex(cur, next, dc / (dc - dn), &work[m++]);
            }
        }
        n = m;
        if (n < 3) {
            return 0;                 /* entièrement rejeté */
        }
        for (i = 0; i < n; i++) {
            poly[i] = work[i];
        }
    }

    /* Le polygone est retriangulé en éventail. Ne garder que le premier triangle
       ferait disparaître le reste de la surface — un trou, sur les seuls
       triangles découpés, donc rare et déroutant.

       `out` en contient six, soit deux triangles : c'est le contrat de cette
       fonction, et un polygone plus riche est tronqué plutôt que de déborder.
       Le cas ne se présente que sur des triangles qui traversent plusieurs plans
       à la fois, où la surface perdue est hors de la bande de garde. */
    triangles = n - 2;
    if (triangles > 2) {
        triangles = 2;
    }
    for (i = 0; i < triangles; i++) {
        out[i * 3 + 0] = poly[0];
        out[i * 3 + 1] = poly[i + 1];
        out[i * 3 + 2] = poly[i + 2];
    }
    return triangles;
}

void dkr_clip_project(const dkr_transform *t, const dkr_clip_vertex *in,
                      dkr_render_vertex *out)
{
    float oow;

    if (!t || !in || !out) {
        return;
    }
    /* Le découpage garantit `w > epsilon` : la division est sûre ici, et c'est
       tout l'intérêt de l'avoir faite avant. */
    oow = 1.0f / in->w;

    memset(out, 0, sizeof(*out));
    out->x = in->x * oow * t->viewport_scale_x + t->viewport_trans_x;
    out->y = in->y * oow * t->viewport_scale_y + t->viewport_trans_y;

    /* **La profondeur est bornee a [0,1], et ce n'est pas un ajustement.**
     *
     * Le tampon de profondeur est defini sur cet intervalle : une valeur en
     * dehors n'a pas de sens, et elle gagne le test partout. Un sommet cree par
     * le decoupage sort avec `w` egal a la marge du plan proche, donc une
     * profondeur enorme — mesure : -250000 pour une marge de 0,0001 — qui passe
     * devant toute la scene.
     *
     * Le symptome est spectaculaire et trompeur : des pixels isoles du triangle
     * decoupe percent a travers une surface qui devrait le masquer, en un motif
     * poinstille qui evoque un defaut de rasterisation plutot qu'un defaut de
     * profondeur. Il a fallu projeter un sommet a la main pour le voir.
     *
     * Le materiel reel borne de meme. */
    {
        const float z = in->z * oow;
        out->z = (z < 0.0f) ? 0.0f : (z > 1.0f ? 1.0f : z);
    }
    out->ooz = out->z;
    out->oow = oow;
    out->r = in->r;
    out->g = in->g;
    out->b = in->b;
    out->a = in->a;
    /* Les coordonnées de texture sont divisées ici, pas avant : le rastériseur
       et Glide attendent `s/w` et `t/w`.
       L'échelle de 256 est la convention de Glide, mesurée sur la carte — voir
       `DKR_TEXCOORD_SCALE` dans `backend.h`. Elle est appliquée ici, une fois
       par sommet, plutôt que par le backend une fois par triangle. */
    out->tmu[0][DKR_TMU_SOW] = in->s * DKR_TEXCOORD_SCALE * oow;
    out->tmu[0][DKR_TMU_TOW] = in->t * DKR_TEXCOORD_SCALE * oow;
    out->tmu[0][DKR_TMU_OOW] = oow;
}

/* --- Faces arrière ---------------------------------------------------------- */

dkr_cull_mode dkr_cull_mode_for_viewport(float viewport_scale_x, int cull_enabled)
{
    if (!cull_enabled) {
        return DKR_CULL_NONE;
    }
    /* Convention du microcode, relevée dans `f3ddkr_rt64.cpp` : le sens dépend
       du **signe de l'échelle en x**. Une fenêtre miroir inverse l'orientation
       apparente des triangles, et éliminer le mauvais côté viderait l'écran —
       défaut spectaculaire et facile à mal diagnostiquer. */
    return (viewport_scale_x > 0.0f) ? DKR_CULL_BACK : DKR_CULL_FRONT;
}

int dkr_cull_accept(const dkr_render_vertex v[3], dkr_cull_mode mode)
{
    float area;
    if (!v || mode == DKR_CULL_NONE) {
        return 1;
    }
    /* L'aire signée en espace écran, origine en haut à gauche. Le rastériseur de
       E04-S08 emploie la même convention : les deux doivent coïncider, sans quoi
       l'oracle et Glide n'élimineraient pas les mêmes triangles. */
    area = (v[1].x - v[0].x) * (v[2].y - v[0].y) -
           (v[2].x - v[0].x) * (v[1].y - v[0].y);
    if (area == 0.0f) {
        return 0;                     /* dégénéré : rien à dessiner */
    }
    return (mode == DKR_CULL_BACK) ? (area < 0.0f) : (area > 0.0f);
}

/* --- Rejet ------------------------------------------------------------------ */

int dkr_clip_reject_offscreen(const dkr_render_vertex v[3],
                              int width, int height, float margin)
{
    int i;
    int left = 0, right = 0, above = 0, below = 0;

    if (!v) {
        return 1;
    }
    /* Rejeté seulement si **les trois** sommets sont du même côté. Un triangle
       dont les sommets sont dispersés de part et d'autre couvre peut-être
       l'écran, et le rejeter serait une erreur bien plus grave que de laisser
       passer un triangle inutile. */
    for (i = 0; i < 3; i++) {
        if (v[i].x < -margin)                    { left++; }
        if (v[i].x > (float)width + margin)      { right++; }
        if (v[i].y < -margin)                    { above++; }
        if (v[i].y > (float)height + margin)     { below++; }
    }
    return left == 3 || right == 3 || above == 3 || below == 3;
}

/* --- Fenêtre de ciseaux ------------------------------------------------------ */

int dkr_scissor_for_player(int players, int player, int width, int height,
                           dkr_scissor *out)
{
    if (!out || player < 0 || player >= players || width <= 0 || height <= 0) {
        return 0;
    }
    switch (players) {
    case 1:
        out->x0 = 0; out->y0 = 0; out->x1 = width; out->y1 = height;
        return 1;
    case 2:
        /* Deux joueurs : partage horizontal, l'un au-dessus de l'autre. */
        out->x0 = 0; out->x1 = width;
        out->y0 = player * (height / 2);
        out->y1 = out->y0 + height / 2;
        return 1;
    case 3:
    case 4:
        /* Trois et quatre joueurs partagent la même grille de quatre quadrants ;
           à trois, le quatrième reste vide. Les traiter ensemble évite deux
           calculs qui divergeraient. */
        out->x0 = (player % 2) * (width / 2);
        out->y0 = (player / 2) * (height / 2);
        out->x1 = out->x0 + width / 2;
        out->y1 = out->y0 + height / 2;
        return 1;
    default:
        return 0;
    }
}
