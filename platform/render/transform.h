/* E04-S03 — matrix stack and vertex transformation.
 *
 * On the N64 the RSP does the transforming. Here it falls to the host CPU — as
 * on any 3dfx card, which transforms nothing. **It is the heaviest graphics
 * computation of the port**, and it lands entirely inside the E00-S03 budget.
 *
 * ## N64 matrices are not ordinary matrices
 *
 * They are 16.16 fixed point, and **stored as two separate halves**: the sixteen
 * integer parts first, the sixteen fractional parts next. `gbi.h` says it in one
 * sentence — "First 8 words are integer portion of the 4x4 matrix, last 8 words
 * are the fraction portion" — and departing from it produces not an error but
 * wrong geometry.
 *
 * ## The stack depth is measured, not assumed
 *
 * `f3ddkr_rt64.cpp` clamps the matrix index to 2 in `Matrix` as in `MoveWord`:
 * **DKR uses three slots**. Providing sixteen out of caution would cost memory
 * on a machine that has none, and would hide a mis-decoded command aiming at a
 * slot that does not exist.
 */
#ifndef DKR_RENDER_TRANSFORM_H
#define DKR_RENDER_TRANSFORM_H

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration: `clip.h` includes this file, and the reverse would make
   a cycle. The complete type is defined over there. */
struct dkr_clip_vertex_;

#define DKR_MATRIX_SLOTS 3

typedef struct {
    /* Stored column-major, like the microcode: `m[column][row]`. */
    float m[4][4];
} dkr_matrix;

/* --- 16.16 conversion ------------------------------------------------------- *
 *
 * `data` is 64 bytes: 32 for the integer parts, 32 for the fractional ones. The
 * read is **big-endian**, like RDRAM.
 *
 * Returns 0 if `data` is null. There is no other way to fail: every combination
 * of 64 bytes describes a matrix, however absurd. */
int dkr_matrix_from_fixed(const unsigned char *data, dkr_matrix *out);

/* --- The stack -------------------------------------------------------------- */
typedef struct {
    dkr_matrix slot[DKR_MATRIX_SLOTS];
    dkr_matrix projection;
    dkr_matrix mvp;              /* product, recomputed on demand */
    int        selected;         /* 0..2 */
    int        mvp_valid;

    /* Viewport. The x scale carries the sign that decides the culling
       direction — that is how the microcode expresses it, and the decoder
       depends on it. */
    float viewport_scale_x, viewport_scale_y;
    float viewport_trans_x, viewport_trans_y;
} dkr_transform;

void dkr_transform_init(dkr_transform *t);
void dkr_transform_set_matrix(dkr_transform *t, int slot, const dkr_matrix *m);
void dkr_transform_select(dkr_transform *t, int slot);
void dkr_transform_set_projection(dkr_transform *t, const dkr_matrix *m);
void dkr_transform_set_viewport(dkr_transform *t, float sx, float sy,
                                float tx, float ty);

/* --- The vertex ------------------------------------------------------------- *
 *
 * The DKR vertex as it sits in RDRAM: ten bytes, position as signed 16-bit
 * integers then colour as bytes. **No texture coordinates** — they arrive per
 * corner when the triangle is emitted. */
typedef struct {
    short         x, y, z;
    unsigned char r, g, b, a;
} dkr_source_vertex;

/* Transforms and **writes the backend format directly**, with no intermediate
   copy: `dkr_render_vertex` has the layout of `GrVertex`, and a per-vertex
   conversion would cost dearly on a Pentium II that sees tens of thousands of
   them per frame.
 *
 * Returns 0 if the vertex is behind the projection plane — `w <= 0` — in which
 * case `out` is not written. Clipping proper is E04-S05; here we merely refrain
 * from dividing by a value that means nothing. */
int dkr_transform_vertex(dkr_transform *t, const dkr_source_vertex *in,
                         dkr_render_vertex *out);

/* Transforms into **homogeneous space**, without dividing.
 *
 * That is what clipping needs: dividing before clipping produces meaningless
 * coordinates for vertices behind the camera, and that is precisely what E04-S05
 * exists to avoid. `dkr_transform_vertex` stays available for the cases where we
 * know no clipping is needed.
 *
 * `s` and `t` are laid down as they are — not divided — because they arrive with
 * the triangle and not with the vertex. */
void dkr_transform_to_clip(dkr_transform *t, const dkr_source_vertex *in,
                           float s, float tc, struct dkr_clip_vertex_ *out);

/* The current model-view-projection matrix, recomputed if needed. Exposed for
   the tests and for E08-S03, which will want to process it in batches. */
const dkr_matrix *dkr_transform_mvp(dkr_transform *t);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_TRANSFORM_H */
