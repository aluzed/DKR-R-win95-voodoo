/* E04-S01 — l'implémentation vide.
 *
 * Elle accepte tout et ne dessine rien. Deux raisons de l'écrire, et aucune
 * n'est la complaisance :
 *
 *   - elle établit que l'interface **se compile et se lie** sans backend réel,
 *     ce que E04-S01 demande explicitement ;
 *   - elle donne au décodeur une cible pendant que Glide (E05) et le rastériseur
 *     logiciel (E04-S08) s'écrivent, de sorte que E04-S02 n'attende personne.
 *
 * Elle compte ce qu'elle reçoit. Un décodeur qui n'émet aucun triangle et un
 * backend qui n'en dessine aucun se ressemblent beaucoup vus de l'écran ; le
 * compteur les distingue.
 */
#include "backend.h"

#include <string.h>

typedef struct {
    int                width, height;
    int                open;
    unsigned long long frames;
    unsigned long long triangles;
    unsigned long long rects;
    unsigned long long state_changes;
    dkr_texture_handle next_handle;
    dkr_render_state   state;
} null_backend;

/* Un seul exemplaire : l'implémentation vide n'a aucune raison d'être
   instanciée deux fois, et lui donner un allocateur ferait dépendre de `malloc`
   un objet dont le rôle est de ne rien faire. */
static null_backend g_null;

static int null_open(void *self, int width, int height)
{
    null_backend *b = (null_backend *)self;
    b->width  = width;
    b->height = height;
    b->open   = 1;
    return 1;
}

static void null_close(void *self)
{
    ((null_backend *)self)->open = 0;
}

static void null_begin_frame(void *self, unsigned clear_argb)
{
    (void)clear_argb;
    ((null_backend *)self)->frames++;
}

static void null_present(void *self)
{
    (void)self;
}

static void null_set_state(void *self, const dkr_render_state *state)
{
    null_backend *b = (null_backend *)self;
    if (!state) {
        return;
    }
    /* Le bloc est comparable par `memcmp` — c'est une propriété de l'interface,
       et l'exercer ici la met à l'épreuve : si quelqu'un y glissait un jour un
       remplissage implicite, ce compteur deviendrait erratique. */
    if (memcmp(&b->state, state, sizeof(*state)) != 0) {
        b->state = *state;
        b->state_changes++;
    }
}

static void null_set_scissor(void *self, int x0, int y0, int x1, int y1)
{
    (void)self; (void)x0; (void)y0; (void)x1; (void)y1;
}

static void null_draw_triangles(void *self, const dkr_render_vertex *vertices,
                                int triangle_count)
{
    (void)vertices;
    if (triangle_count > 0) {
        ((null_backend *)self)->triangles += (unsigned long long)triangle_count;
    }
}

static void null_fill_rect(void *self, int x0, int y0, int x1, int y1,
                           unsigned argb)
{
    (void)x0; (void)y0; (void)x1; (void)y1; (void)argb;
    ((null_backend *)self)->rects++;
}

static dkr_texture_handle null_texture_upload(void *self,
                                              const dkr_texture_desc *desc)
{
    null_backend *b = (null_backend *)self;
    (void)desc;
    /* Les handles commencent à 1 : zéro veut dire « aucune texture ». */
    return ++b->next_handle;
}

static void null_texture_release(void *self, dkr_texture_handle handle)
{
    (void)self; (void)handle;
}

void dkr_render_backend_null(dkr_render_backend *out)
{
    if (!out) {
        return;
    }
    memset(&g_null, 0, sizeof(g_null));
    out->name            = "null";
    out->open            = null_open;
    out->close           = null_close;
    out->begin_frame     = null_begin_frame;
    out->present         = null_present;
    out->set_state       = null_set_state;
    out->set_scissor     = null_set_scissor;
    out->draw_triangles  = null_draw_triangles;
    out->fill_rect       = null_fill_rect;
    out->texture_upload  = null_texture_upload;
    out->texture_release = null_texture_release;
    out->self            = &g_null;
}
