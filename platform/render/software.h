/* E04-S08 — the reference software rasteriser.
 *
 * Its reason to exist fits in one sentence: **when an image comes out wrong
 * under Glide, we will need to know whether the error comes from the decoder or
 * from the backend.** Without an intermediate oracle, a wrong pixel can come
 * from ten stages — transform, clipping, texture decoding, combiner
 * translation, Glide setup, driver. With a software backend implementing the
 * **same interface** (E04-S01), the question is settled in a single run.
 *
 * ## It is allowed to be slow, and not allowed to be complicated
 *
 * Its entire value rests on the trust placed in it as a reference. An optimised
 * rasteriser is a rasteriser whose own correctness has to be checked, and the
 * oracle vanishes. The code below therefore picks the most obvious form every
 * time: floats everywhere, no tiling, no precomputed table, one loop per pixel
 * of the bounding box.
 *
 * ## What it does not borrow from Glide
 *
 * Nothing. That is the point. Where the Glide backend will have to bend the
 * RDP's combiner to the card's modes, this one computes what the image
 * **should** be, and E05-S03 will measure the gap against that reference.
 */
#ifndef DKR_RENDER_SOFTWARE_H
#define DKR_RENDER_SOFTWARE_H

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fills `out` with the software backend. `open` allocates the buffers.
 *
 * One instance at a time, as with the empty implementation: two simultaneous
 * oracles make no sense, and an instance allocator would be code nobody
 * calls. */
void dkr_render_backend_software(dkr_render_backend *out);

/* --- What an oracle has to make observable --------------------------------- *
 *
 * The three functions below have no counterpart in the interface, and rightly
 * so: they do not serve rendering but **comparison**, which is this backend's
 * job. The E09-S02 harness is their consumer.
 */

/* The image, in 32-bit ARGB, rows top to bottom. Returned directly rather than
   copied: the harness compares, it does not modify. */
const unsigned *dkr_software_framebuffer(int *width, int *height);

/* Writes the image as a 24-bit BMP. That format because it takes thirty lines
   to write with no library, and because both Windows 95 and the host read it —
   a PNG would demand zlib on both sides for no gain here.

   Returns 0 on failure. */
int dkr_software_write_bmp(const char *path);

/* --- The probe: which state painted this pixel ------------------------------ *
 *
 * `dkr_software_probe(x, y)` arms it; the next frame records, for that pixel,
 * **every** draw that writes it, in order — the state, what was underneath and
 * what was left.
 *
 * It exists because the first question about a divergent pixel is always "what
 * drew that", and answering it by reading the display list by hand is a morning.
 * The rasteriser already holds the answer at the moment it writes.
 *
 * The history and not merely the last writer, because the first pixel this was
 * pointed at had been painted **seven times**: DKR draws its text in passes, and
 * which pass supplies a colour is exactly the question a record of the last one
 * cannot answer.
 *
 * `dkr_software_probe_result` returns the **total** number of writes and sets
 * `kept` to how many the log holds. The two differ when a pixel is painted more
 * than `DKR_PROBE_WRITES` times, and the difference is what says so. */
#define DKR_PROBE_WRITES 16

typedef struct {
    dkr_render_state state;
    unsigned         before;   /* what was in the buffer */
    unsigned         after;    /* what this draw left */
} dkr_probe_write;

void dkr_software_probe(int x, int y);
int  dkr_software_probe_result(const dkr_probe_write **log, int *kept);

/* --- The textures, as the oracle holds them --------------------------------- *
 *
 * Every format is converted to 32-bit ARGB on upload, so one accessor shows what
 * any of them became. It answers the question that follows "what drew this
 * pixel": *with what*. A quad that samples one colour over its whole surface has
 * either the wrong texture or degenerate coordinates, and only looking at the
 * texture separates the two.
 *
 * `slot` is the handle minus one, as `dkr_render_state.texture` carries it.
 * Returns NULL for a slot that holds nothing, which is the answer when a draw
 * names a texture that was never uploaded. */
const unsigned *dkr_software_texture(int slot, int *width, int *height,
                                     unsigned long long *key);

/* How many pixels this texture actually painted over the frame.
 *
 * "Uploaded" and "reached the screen" are different facts, and only the second
 * says whether an object is in the image. A texture uploaded and never sampled
 * is an object that is missing, which no upload counter can tell you. */
unsigned long dkr_software_texture_pixels(int slot);

/* And how many triangles were drawn with it. Zero pixels has two causes needing
   opposite answers — nothing drawn while it was bound, or something drawn that
   covered no pixel — and the pixel count alone cannot tell them apart. */
unsigned long dkr_software_texture_triangles(int slot);

/* How much of the frame each category of combiner configuration painted, indexed
 * by `dkr_cc_category`, with a fifth slot (4) for a state naming no catalogue
 * entry at all.
 *
 * **This is the fill figure E05-S03 could not get.** Fill is what limits a
 * Voodoo 2 at 640x480, so the cost of implementing multipass is the area the
 * multipass entries cover, doubled — and the share of the frame those entries
 * paint is also the share the card currently renders through an approximation.
 * Counted here because the oracle is the one that knows what the pixel should
 * have been; the card cannot report on a configuration it cannot express. */
unsigned long dkr_software_category_pixels(int category);

/* The same fill broken down by catalogue entry — `recipe` as the state carries
 * it, one-based, zero meaning none.
 *
 * The category says how much of the frame the card is approximating; this says
 * **which configurations to work on**. "90 % is multipass" is a problem; "three
 * entries account for 85 % of it" is a task. */
unsigned long dkr_software_recipe_pixels(int recipe);

/* Fill computed through a two-cycle configuration, and the part of it where the
 * second cycle **actually changed the pixel**.
 *
 * The two are not the same number and the difference decides a design. A second
 * cycle that is a lerp toward a constant by that constant's alpha is the identity
 * when the alpha is zero, so the cost of a second pass on the card is not the
 * multipass fill — it is the multipass fill that is not a no-op, which is a
 * property of the constants at draw time and not of the configuration. */
/* `which`: 0 = all two-cycle fill, 1 = where the second cycle changes the
 * **colour**, 2 = where it changes the **alpha**.
 *
 * The two are counted apart because they have different remedies, and because a
 * single figure was misleading: on the race it reported 8,872 pixels whose second
 * cycle "does something" while the card, guarding on the environment's alpha,
 * drew zero second passes — and both were right. The colour lerp is the identity
 * there; what moves is the alpha, through a stage of its own. */
unsigned long dkr_software_second_cycle_pixels(int which);

/* The same effective fill, by catalogue entry. "A fifth of the divergence" is a
 * result; "which configuration is the next fifth" is a plan, and this is where
 * that plan comes from — without going near the machine. */
unsigned long dkr_software_second_cycle_by_recipe(int recipe);

/* Which configuration painted each pixel last — `recipe` as the state carries it,
 * 0 where nothing painted and 255 where the state named no catalogue entry.
 *
 * The counters say how much fill each configuration takes; this says **where**.
 * Crossed with a difference map it answers the question that decides what to
 * write next: of the pixels where the card disagrees, which configuration drew
 * them. Guessing that from the totals is how one implements the shape with the
 * largest fill and finds it was not the one that was wrong. */
const unsigned char *dkr_software_recipe_map(int *width, int *height);

/* Depth, for the cases where depth is the suspect. `NULL` if no context is
   open. */
const float *dkr_software_depthbuffer(int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_SOFTWARE_H */
