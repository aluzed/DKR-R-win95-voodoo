/* E04-S03 — mise en œuvre. Le contrat est dans `transform.h`. */
#include "transform.h"
#include "clip.h"

#include <string.h>

/* --- Conversion 16.16 ------------------------------------------------------- */

static short read_s16_be(const unsigned char *p)
{
    return (short)(((unsigned)p[0] << 8) | p[1]);
}

static unsigned short read_u16_be(const unsigned char *p)
{
    return (unsigned short)(((unsigned)p[0] << 8) | p[1]);
}

int dkr_matrix_from_fixed(const unsigned char *data, dkr_matrix *out)
{
    int i, j;
    if (!data || !out) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            const int k = i * 4 + j;
            /* Les deux moitiés sont à 32 octets d'écart, et l'élément se
               recompose en un entier 32 bits **signé** :
               `(entier << 16) | fraction`, divisé par 65536.
             *
               Écrire `entier + fraction / 65536.0` donne le même résultat — la
               décomposition en complément à deux est exacte — mais **traiter la
               fraction comme signée ne le donnerait pas**. C'est l'erreur que
               l'épreuve cherche. */
            const short          hi = read_s16_be(data + k * 2);
            const unsigned short lo = read_u16_be(data + 32 + k * 2);
            const int combined = (int)(((unsigned int)(unsigned short)hi << 16) |
                                       (unsigned int)lo);
            out->m[i][j] = (float)combined / 65536.0f;
        }
    }
    return 1;
}

/* --- La pile ---------------------------------------------------------------- */

static void identity(dkr_matrix *m)
{
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            m->m[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
}

void dkr_transform_init(dkr_transform *t)
{
    int i;
    if (!t) { return; }
    memset(t, 0, sizeof(*t));
    for (i = 0; i < DKR_MATRIX_SLOTS; i++) {
        identity(&t->slot[i]);
    }
    identity(&t->projection);
    identity(&t->mvp);
    t->mvp_valid = 1;
    /* Une fenêtre plausible par défaut : 640x480, origine au centre. Elle sera
       remplacée par celle du jeu ; l'avoir non nulle évite qu'un oubli produise
       tous les sommets au même point, ce qui ressemble à un bug de matrice. */
    t->viewport_scale_x = 320.0f;
    t->viewport_scale_y = -240.0f;
    t->viewport_trans_x = 320.0f;
    t->viewport_trans_y = 240.0f;
}

void dkr_transform_set_matrix(dkr_transform *t, int slot, const dkr_matrix *m)
{
    if (!t || !m || slot < 0 || slot >= DKR_MATRIX_SLOTS) { return; }
    t->slot[slot] = *m;
    t->mvp_valid = 0;
}

void dkr_transform_select(dkr_transform *t, int slot)
{
    if (!t) { return; }
    if (slot < 0) { slot = 0; }
    if (slot >= DKR_MATRIX_SLOTS) { slot = DKR_MATRIX_SLOTS - 1; }
    if (t->selected != slot) {
        t->selected = slot;
        t->mvp_valid = 0;
    }
}

void dkr_transform_set_projection(dkr_transform *t, const dkr_matrix *m)
{
    if (!t || !m) { return; }
    t->projection = *m;
    t->mvp_valid = 0;
}

void dkr_transform_set_viewport(dkr_transform *t, float sx, float sy,
                                float tx, float ty)
{
    if (!t) { return; }
    t->viewport_scale_x = sx;
    t->viewport_scale_y = sy;
    t->viewport_trans_x = tx;
    t->viewport_trans_y = ty;
}

static void multiply(const dkr_matrix *a, const dkr_matrix *b, dkr_matrix *out)
{
    int i, j, k;
    dkr_matrix r;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) {
                s += a->m[i][k] * b->m[k][j];
            }
            r.m[i][j] = s;
        }
    }
    *out = r;
}

const dkr_matrix *dkr_transform_mvp(dkr_transform *t)
{
    if (!t) { return NULL; }
    if (!t->mvp_valid) {
        /* Le produit n'est recalculé qu'au changement. Sur un Pentium II,
           soixante-quatre multiplications par sommet plutôt que par matrice
           serait le genre de dépense qui décide d'un portage. */
        multiply(&t->slot[t->selected], &t->projection, &t->mvp);
        t->mvp_valid = 1;
    }
    return &t->mvp;
}

/* --- Le sommet -------------------------------------------------------------- */

int dkr_transform_vertex(dkr_transform *t, const dkr_source_vertex *in,
                         dkr_render_vertex *out)
{
    const dkr_matrix *m;
    float x, y, z, w, oow;

    if (!t || !in || !out) {
        return 0;
    }
    m = dkr_transform_mvp(t);

    x = (float)in->x;
    y = (float)in->y;
    z = (float)in->z;

    {
        const float cx = x * m->m[0][0] + y * m->m[1][0] + z * m->m[2][0] + m->m[3][0];
        const float cy = x * m->m[0][1] + y * m->m[1][1] + z * m->m[2][1] + m->m[3][1];
        const float cz = x * m->m[0][2] + y * m->m[1][2] + z * m->m[2][2] + m->m[3][2];
        const float cw = x * m->m[0][3] + y * m->m[1][3] + z * m->m[2][3] + m->m[3][3];
        x = cx; y = cy; z = cz; w = cw;
    }

    /* Derrière le plan de projection : on ne divise pas. Le découpage est
       E04-S05 ; ici on refuse simplement de produire un sommet dont les
       coordonnées n'auraient aucun sens — et un sommet aberrant qui traverse
       l'écran est bien plus visible qu'un sommet absent. */
    if (w <= 0.0f) {
        return 0;
    }
    oow = 1.0f / w;

    out->x = x * oow * t->viewport_scale_x + t->viewport_trans_x;
    out->y = y * oow * t->viewport_scale_y + t->viewport_trans_y;
    out->z = z * oow;

    /* `oow` est ce que Glide appelle `1/w`, et `ooz` la valeur du tampon de
       profondeur. Les écrire ici plutôt que plus loin évite la recopie que
       E04-S01 cherche justement à supprimer. */
    out->oow = oow;
    out->ooz = out->z;

    out->r = (float)in->r;
    out->g = (float)in->g;
    out->b = (float)in->b;
    out->a = (float)in->a;

    /* Les coordonnées de texture ne viennent pas du sommet — elles arrivent par
       coin au moment du triangle. On les laisse à zéro plutôt que d'inventer :
       une valeur plausible masquerait un triangle qui aurait oublié de les
       poser. */
    memset(out->tmu, 0, sizeof(out->tmu));
    out->tmu[0][DKR_TMU_OOW] = oow;

    return 1;
}

void dkr_transform_to_clip(dkr_transform *t, const dkr_source_vertex *in,
                           float s, float tc, struct dkr_clip_vertex_ *out)
{
    const dkr_matrix *m;
    float x, y, z;

    if (!t || !in || !out) {
        return;
    }
    m = dkr_transform_mvp(t);
    x = (float)in->x;
    y = (float)in->y;
    z = (float)in->z;

    out->x = x * m->m[0][0] + y * m->m[1][0] + z * m->m[2][0] + m->m[3][0];
    out->y = x * m->m[0][1] + y * m->m[1][1] + z * m->m[2][1] + m->m[3][1];
    out->z = x * m->m[0][2] + y * m->m[1][2] + z * m->m[2][2] + m->m[3][2];
    out->w = x * m->m[0][3] + y * m->m[1][3] + z * m->m[2][3] + m->m[3][3];

    out->r = (float)in->r;
    out->g = (float)in->g;
    out->b = (float)in->b;
    out->a = (float)in->a;
    out->s = s;
    out->t = tc;
}
