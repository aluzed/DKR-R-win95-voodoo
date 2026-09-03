/* E09-S02 — comparing two renderings of the same capture, on the host.
 *
 *   compare reference.bmp candidate.bmp [difference.bmp]
 *
 * **Why the comparison happens here and not on the target.** The oracle's image
 * is produced on the development machine by `replay`; the card's is produced on
 * the Windows 95 machine by `REPLAY.EXE` and carried back on the transfer disk.
 * One of the two has to travel whatever happens, and the host is where a
 * difference map can actually be looked at — which is the half of this harness
 * that E09-S02 recorded as missing: the metrics were computed, the difference
 * image was not, so every divergence had to be re-derived by hand.
 *
 * The metric itself is `platform/render/imagecmp.c`, shared with the target's
 * `COMPARE.EXE`. Two copies of it would drift, and the drift would read as the
 * card disagreeing with the oracle.
 *
 * **The reference is the first argument and that is not arbitrary.** It is
 * quantised to 565 before subtraction, because the card cannot store more; doing
 * it the other way round would count the oracle's extra bits as the card's error.
 */

#include "render/imagecmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *ref_path = 0, *got_path = 0, *diff_path = 0;
    unsigned *ref = 0, *got = 0, *diff = 0;
    int rw = 0, rh = 0, gw = 0, gh = 0;
    dkr_image_metrics m;
    /* The bounds `test_compare.c` measured on the synthetic scene: worst gap 9
       over 307200 pixels, bound set at 16 — two quantisation steps, above the
       noise and below anything with visual meaning. */
    int max_gap_bound = 16;
    long differ_ppm_bound = 10000;   /* one per cent */
    int report_only = 0;
    int i, status = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--report-only") == 0) {
            report_only = 1;
        } else if (strcmp(argv[i], "--max-gap") == 0 && i + 1 < argc) {
            max_gap_bound = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--differ-ppm") == 0 && i + 1 < argc) {
            differ_ppm_bound = atol(argv[++i]);
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "compare: unknown option %s\n", argv[i]);
            return 2;
        } else if (!ref_path) { ref_path = argv[i];
        } else if (!got_path) { got_path = argv[i];
        } else if (!diff_path) { diff_path = argv[i];
        } else {
            fprintf(stderr, "compare: too many files\n");
            return 2;
        }
    }
    if (!ref_path || !got_path) {
        fprintf(stderr,
                "usage: %s [--report-only] [--max-gap N] [--differ-ppm N]\n"
                "          reference.bmp candidate.bmp [difference.bmp]\n",
                argv[0]);
        return 2;
    }

    if (!dkr_image_read_bmp(ref_path, &ref, &rw, &rh)) { return 1; }
    if (!dkr_image_read_bmp(got_path, &got, &gw, &gh)) { free(ref); return 1; }

    /* Checked before anything is subtracted. Two images of different sizes
       compared row by row give a diagonal smear that looks like a rendering
       fault and is not one. */
    if (rw != gw || rh != gh) {
        fprintf(stderr, "compare: %s is %dx%d and %s is %dx%d\n",
                ref_path, rw, rh, got_path, gw, gh);
        free(ref); free(got);
        return 1;
    }

    dkr_image_compare(ref, got, rw, rh, &m);

    printf("reference %s, candidate %s, %dx%d\n", ref_path, got_path, rw, rh);
    printf("  painted surface: reference %ld, candidate %ld (%ld%% gap)\n",
           m.painted_ref, m.painted_got,
           m.painted_ref ? (100 * (m.painted_got - m.painted_ref) / m.painted_ref)
                         : 0L);
    printf("  frankly different pixels: %ld out of %ld (%ld per million)\n",
           m.differ, m.total, dkr_image_per_million(m.differ, m.total));
    printf("  of which on an edge, counted separately: %ld\n", m.differ_edge);
    printf("  worst per-channel gap over the whole image: %d\n", m.max_gap);
    if (m.worst) {
        const size_t k = (size_t)m.worst_y * (size_t)rw + (size_t)m.worst_x;
        printf("  worst gap off-edge: %d at (%d,%d)  "
               "reference 0x%06X  candidate 0x%06X\n",
               m.worst, m.worst_x, m.worst_y,
               dkr_image_to565(ref[k] & 0x00FFFFFFu), got[k] & 0x00FFFFFFu);
    }

    if (diff_path) {
        diff = (unsigned *)malloc((size_t)rw * (size_t)rh * sizeof(unsigned));
        if (!diff) {
            fprintf(stderr, "compare: cannot allocate the difference map\n");
        } else {
            dkr_image_diff_map(ref, got, rw, rh, diff);
            if (dkr_image_write_bmp(diff_path, diff, rw, rh)) {
                printf("  difference map written to %s\n", diff_path);
            } else {
                fprintf(stderr, "compare: cannot write %s\n", diff_path);
            }
            free(diff);
        }
    }

    if (!report_only) {
        const long ppm = dkr_image_per_million(m.differ, m.total);
        if (m.total == 0) {
            printf("VERDICT: nothing compared\n");
            status = 1;
        } else if (m.max_gap > max_gap_bound || ppm > differ_ppm_bound) {
            /* The verdict names which bound gave way. "differs" would leave the
               reader to work out whether a handful of pixels are badly wrong or
               a large area is slightly wrong, and those are different faults. */
            printf("VERDICT: differs — %s\n",
                   (m.max_gap > max_gap_bound)
                     ? "the worst gap exceeds the bound"
                     : "too large a share of the image differs frankly");
            status = 1;
        } else {
            printf("VERDICT: agrees within %d per channel and %ld per million\n",
                   max_gap_bound, differ_ppm_bound);
        }
    }

    free(ref);
    free(got);
    return status;
}
