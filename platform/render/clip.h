/* E04-S05 — clipping, back faces and the scissor window.
 *
 * ## Why clipping is needed, and why only at the near plane
 *
 * 3dfx cards do not clip. They have a scissor window that rejects out-of-area
 * fragments, which is enough on the sides — but a triangle with one vertex
 * **behind the camera** cannot be rejected at the fragment level: its projection
 * is mathematically absurd, and the vertex comes out the other side of the
 * screen. That is the shard of geometry crossing the image, very visible and
 * hard to reproduce because it depends on one precise angle.
 *
 * **Only the near plane therefore demands real clipping.** Full clipping against
 * all six planes would cost a lot and add nothing the scissor window does not
 * already do. That distinction is the key to this stage's cost.
 *
 * ## The attribute one forgets
 *
 * Clipping produces new vertices, and each must carry **every** interpolated
 * attribute: position, colour, texture coordinates. Forgetting one produces an
 * artefact visible only on clipped triangles — hence rare, hence baffling. The
 * test checks each attribute separately for that reason.
 */
#ifndef DKR_RENDER_CLIP_H
#define DKR_RENDER_CLIP_H

#include "backend.h"
#include "transform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A vertex **before** the perspective divide: homogeneous position and
 * attributes. That is where clipping has to happen — after projection it is too
 * late, the divide having already produced meaningless coordinates. */
struct dkr_clip_vertex_ {
    float x, y, z, w;
    float r, g, b, a;
    float s, t;              /* texture coordinates, not divided */
};
typedef struct dkr_clip_vertex_ dkr_clip_vertex;

/* The near plane is not `w > 0` but `w > epsilon`.
 *
 * A vertex exactly on the plane gives an infinite `1/w`; a vertex just in front
 * gives an enormous `1/w` that saturates in floating point and produces the same
 * shards as the vertex behind. The margin is small but it is not zero. */
#ifndef DKR_CLIP_NEAR_EPSILON
#define DKR_CLIP_NEAR_EPSILON 0.0001f
#endif

/* --- The guard band --------------------------------------------------------- *
 *
 * Clipping at the near plane alone is not enough, and measurement showed it: a
 * vertex created at `w = 0.0001` projects to **16 million pixels**. The
 * rasteriser's edge functions then subtract numbers of that magnitude to obtain
 * units — catastrophic cancellation — and the result depends on the precision of
 * the intermediates. The host computes in 32-bit SSE, the target in 80-bit x87:
 * the two then do not render the same pixels.
 *
 * Clipping against a **guard band** as well bounds the projected coordinates by
 * construction, and makes the oracle exact on both sides.
 *
 * This is not the full six-plane clipping that E04-S05 rightly discards: the
 * band is far wider than the screen, so almost no triangle crosses it, and those
 * that stay entirely inside leave through a short circuit without a single edge
 * being computed.
 *
 * `DKR_CLIP_GUARD` is the ratio between the band and the half-screen. At 4, a
 * 640-pixel screen tolerates coordinates from -960 to 1600. The exact value
 * Glide accepts is still to be measured (E04-S05); this one is chosen so that
 * coordinates stay in a range where precision holds, which is a different and
 * independent constraint. */
#ifndef DKR_CLIP_GUARD
#define DKR_CLIP_GUARD 4.0f
#endif

/* Clips a triangle at the near plane.
 *
 * `out` receives 0, 1 or 2 triangles — three vertices each — and must therefore
 * be able to hold six. Returns the number of triangles produced.
 *
 * Two triangles for one: that is the case where **exactly one** vertex is
 * behind. The remaining polygon is then a quadrilateral, which has to be
 * retriangulated. Forgetting that makes half the surface disappear, which shows
 * up as a hole. */
int dkr_clip_near(const dkr_clip_vertex in[3], dkr_clip_vertex out[6]);

/* Projects a clipped vertex into the backend's format. The perspective divide
   happens here, once clipping has made it safe. */
void dkr_clip_project(const dkr_transform *t, const dkr_clip_vertex *in,
                      dkr_render_vertex *out);

/* --- Back faces ------------------------------------------------------------- *
 *
 * The card does not cull them either. The convention comes from the microcode:
 * `f3ddkr_rt64.cpp` picks `G_CULL_BACK` or `G_CULL_FRONT` according to the
 * **sign of the viewport's x scale**, and bit 0x40 of the triangle header
 * disables culling.
 *
 * Returns 1 if the triangle is to be drawn. */
int dkr_cull_accept(const dkr_render_vertex v[3], dkr_cull_mode mode);

/* The direction the microcode picks for a given viewport. */
dkr_cull_mode dkr_cull_mode_for_viewport(float viewport_scale_x, int cull_enabled);

/* --- Rejection -------------------------------------------------------------- *
 *
 * A triangle entirely on one side of the screen will be drawn by nobody.
 * Rejecting it here avoids sending it, and every primitive not sent is one call
 * fewer through a DLL.
 *
 * `margin` is the off-screen tolerance: Glide accepts moderately out-of-range
 * coordinates, and clipping too early would cost more than letting them through.
 * **This margin is measured, not deduced**; pending that measurement, the chosen
 * value is deliberately generous. */
int dkr_clip_reject_offscreen(const dkr_render_vertex v[3],
                              int width, int height, float margin);

#define DKR_CLIP_DEFAULT_MARGIN 2048.0f

/* --- Scissor window --------------------------------------------------------- *
 *
 * DKR uses it for split screen. The four layouts are computed here rather than
 * hard-coded at the call site, because this is the kind of computation one gets
 * wrong the second time round. */
typedef struct { int x0, y0, x1, y1; } dkr_scissor;

/* `player` runs from 0 to `players - 1`. Returns 0 if the request makes no
   sense. */
int dkr_scissor_for_player(int players, int player, int width, int height,
                           dkr_scissor *out);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_CLIP_H */
