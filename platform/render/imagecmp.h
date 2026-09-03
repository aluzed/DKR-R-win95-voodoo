#ifndef DKR_RENDER_IMAGECMP_H
#define DKR_RENDER_IMAGECMP_H

/* E09-S02 — the metric that says whether two renderings agree, and the BMP that
 * carries them between the two machines.
 *
 * **Why this is a file and not a function inside the test.** The comparison used
 * to live in `test_compare.c`, where it served one synthetic scene on the target.
 * A replay compares an image made on the Windows 95 machine against an image made
 * on the development machine, so the same metric now has to run in two programs
 * built by two compilers. A second copy of it would drift, and the drift would be
 * read as the card disagreeing with the oracle — which is precisely the sentence
 * this project already wrote about the scene, and then did not apply to the
 * measurement of the scene.
 *
 * ## What the metric has to allow for
 *
 * Three gaps between the two renderings are structural and mean nothing:
 *
 *   - **the card's 565 quantisation** — five bits of red and blue, six of green,
 *     against the rasteriser's eight;
 *   - **depth**, z over [0,1] on one side and an encoded w buffer on the other:
 *     the two order the same way and do not quantise the same way;
 *   - **edges**, where a fill rule differing by half a pixel moves a whole column.
 *
 * So the reference is quantised to 565 before anything is subtracted, and edge
 * pixels are counted in their own column rather than forgiven. A single tight
 * threshold would fail every run for a good reason, and a measurement one stops
 * reading is worse than no measurement.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    long total;        /* pixels examined */
    long painted_ref;  /* non-black in the reference */
    long painted_got;  /* non-black in the image under test */
    long differ;       /* frankly different, away from an edge */
    long differ_edge;  /* frankly different, on an edge, counted apart */
    int  max_gap;      /* worst per-channel gap anywhere, edges included */
    int  worst;        /* worst per-channel gap away from an edge */
    int  worst_x;      /* and where it is, so it can be looked at */
    int  worst_y;
} dkr_image_metrics;

/* A pixel counts as frankly different past this per-channel gap. Above the 565
   noise (measured worst: 9) and far below anything with visual meaning. */
#define DKR_IMAGE_FRANK_GAP 24

/* Quantises a 24-bit colour the way the card stores it, high bits replicated into
   the low ones exactly as the read-back does. Without the replication the
   rasteriser's white and the card's white would not coincide, and every bright
   surface would be counted as a divergence. */
unsigned dkr_image_to565(unsigned c);

/* `ref` is the reference (quantised here, so pass it unquantised), `got` is the
   image under test. Both 32-bit ARGB, rows top to bottom, `w * h` pixels. */
void dkr_image_compare(const unsigned *ref, const unsigned *got,
                       int w, int h, dkr_image_metrics *out);

/* Renders the disagreement as an image: agreement as the reference darkened, so
   the shape of the scene stays readable; a real divergence in red scaled over the
   frank threshold; and a divergence forgiven as an edge in dim amber, so that the
   map and the counts printed beside it tell the same story rather than two.

   Counting divergent pixels says how many; only a map says *where*, and where is
   what tells a shifted column from a wrong texture. */
void dkr_image_diff_map(const unsigned *ref, const unsigned *got,
                        int w, int h, unsigned *out);

/* `part` per million of `total`.
 *
 * A function, for one reason: `1000000L * part / total` overflows a 32-bit `long`
 * the moment `part` passes 2147, and the target's `long` is 32 bits. Measured on
 * 3 September 2026 — the host printed 16507 per million and the Windows 95
 * machine printed 2526 for the same two images. A derived number that is wrong
 * only on the machine under test is the worst kind, because that is the machine
 * whose numbers one is reading. */
long dkr_image_per_million(long part, long total);

/* --- The 24-bit BMP, read and written -------------------------------------- *
 *
 * That format because it takes thirty lines with no library and both machines
 * read it. Written here rather than in each program: this is the third place the
 * same header was being laid out by hand, and one of the three had already got
 * the row order wrong once. */
int dkr_image_write_bmp(const char *path, const unsigned *pixels, int w, int h);

/* Allocates `*pixels` (the caller frees) and fills `*w`, `*h`. Returns 1 on
   success. Refuses with a message naming the check that failed — a top-down BMP,
   a bit depth other than 24, a truncated file: three different problems, and
   "cannot read" would send the reader looking at the wrong one. */
int dkr_image_read_bmp(const char *path, unsigned **pixels, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_IMAGECMP_H */
