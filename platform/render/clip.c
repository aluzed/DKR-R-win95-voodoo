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

int dkr_clip_near(const dkr_clip_vertex in[3], dkr_clip_vertex out[6])
{
    dkr_clip_vertex kept[4];
    int             n = 0;
    int             i;

    if (!in || !out) {
        return 0;
    }

    /* Sutherland-Hodgman sur un seul plan : pour chaque arête, on garde le
       sommet s'il est devant, et l'on ajoute l'intersection si l'arête traverse.
       Le polygone résultant a trois ou quatre sommets. */
    for (i = 0; i < 3; i++) {
        const dkr_clip_vertex *cur  = &in[i];
        const dkr_clip_vertex *next = &in[(i + 1) % 3];
        const int cur_in  = cur->w  > DKR_CLIP_NEAR_EPSILON;
        const int next_in = next->w > DKR_CLIP_NEAR_EPSILON;

        if (cur_in) {
            kept[n++] = *cur;
        }
        if (cur_in != next_in) {
            /* Le paramètre de l'intersection avec `w = epsilon`. Le dénominateur
               ne peut pas s'annuler : les deux sommets sont de part et d'autre,
               donc leurs `w` diffèrent. */
            const float k = (DKR_CLIP_NEAR_EPSILON - cur->w) / (next->w - cur->w);
            lerp_vertex(cur, next, k, &kept[n++]);
        }
    }

    if (n < 3) {
        return 0;                     /* entièrement derrière */
    }
    out[0] = kept[0]; out[1] = kept[1]; out[2] = kept[2];
    if (n == 3) {
        return 1;
    }
    /* Quatre sommets : le polygone est un quadrilatère, et il faut le
       retrianguler. L'oublier ferait disparaître la moitié de la surface — un
       trou, sur les seuls triangles qui traversent le plan. */
    out[3] = kept[0]; out[4] = kept[2]; out[5] = kept[3];
    return 2;
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
       et Glide attendent `s/w` et `t/w`. */
    out->tmu[0][DKR_TMU_SOW] = in->s * oow;
    out->tmu[0][DKR_TMU_TOW] = in->t * oow;
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
