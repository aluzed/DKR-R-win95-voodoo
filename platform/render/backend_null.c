/* E04-S01 — the empty implementation.
 *
 * It accepts everything and draws nothing. There are two reasons to write it,
 * and neither of them is indulgence:
 *
 *   - it establishes that the interface **compiles and links** without a real
 *     backend, which is what E04-S01 explicitly asks for;
 *   - it gives the decoder a target while Glide (E05) and the software
 *     rasteriser (E04-S08) are being written, so that E04-S02 waits for nobody.
 *
 * It counts what it receives. A decoder that emits no triangle and a backend
 * that draws none look very much alike from the screen; the counter tells them
 * apart.
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

/* A single instance: the empty implementation has no reason to be instantiated
   twice, and giving it an allocator would make an object whose whole job is to
   do nothing depend on `malloc`. */
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
    /* The block is comparable with `memcmp` — that is a property of the
       interface, and exercising it here puts it to the test: should anyone ever
       slip implicit padding into it, this counter would turn erratic. */
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
    /* Handles start at 1: zero means "no texture". */
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
