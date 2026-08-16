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

/* Depth, for the cases where depth is the suspect. `NULL` if no context is
   open. */
const float *dkr_software_depthbuffer(int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_SOFTWARE_H */
