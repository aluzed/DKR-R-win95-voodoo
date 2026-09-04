/* E09-S02 — implementation. The contract and the reasoning live in `imagecmp.h`. */

#include "imagecmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>       /* _commit */
#endif

unsigned dkr_image_to565(unsigned c)
{
    const unsigned r = ((c >> 16) & 0xFFu) >> 3;
    const unsigned g = ((c >>  8) & 0xFFu) >> 2;
    const unsigned b = ( c        & 0xFFu) >> 3;
    return (((r << 3) | (r >> 2)) << 16) |
           (((g << 2) | (g >> 4)) <<  8) |
            ((b << 3) | (b >> 2));
}

static int channel_gap(unsigned a, unsigned b)
{
    int worst = 0, i;
    for (i = 0; i < 3; i++) {
        int d = (int)((a >> (i * 8)) & 0xFFu) - (int)((b >> (i * 8)) & 0xFFu);
        if (d < 0) { d = -d; }
        if (d > worst) { worst = d; }
    }
    return worst;
}

/* A pixel is on an edge if one of its neighbours in the *reference* differs
   markedly. The reference and not the image under test: the question asked is
   "is this a place where half a pixel of fill rule moves a column", and that is
   a property of the scene, not of the renderer being judged. */
/* **What "on an edge" has to mean, and what it must not.**
 *
 * The first version of this asked only whether the *reference* changed sharply
 * nearby, and forgave the pixel if it did. That rule was written for the
 * synthetic scene — four large triangles, where the only pixels next to a sharp
 * change are the thin boundaries a half-pixel of fill rule can move.
 *
 * Applied to a real frame it collapses. Measured on 3 September 2026: the card
 * rendered the character's nameplate with **every one of 2416 blue pixels turned
 * to black**, and the old rule reported 186 divergent pixels. The glyphs are one
 * to three pixels wide and outlined, so every pixel of them stands next to a
 * sharp change and the rule absolved the entire object. An instrument that
 * under-reports by a factor of thirteen on the one real defect in the image is
 * worse than no instrument, because its silence is taken for agreement.
 *
 * So the question is asked the way the excuse is actually shaped. A fill rule
 * differing by half a pixel does not invent a colour: it gives the pixel the
 * colour of the surface **next door**. A pixel is therefore forgiven as an edge
 * only if what the card put there matches one of the reference's neighbours. A
 * colour that appears nowhere around it is wrong wherever it sits.
 */
static int is_edge(const unsigned *ref, int w, int h, int x, int y,
                   unsigned got)
{
    int dx, dy;

    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            const int nx = x + dx, ny = y + dy;
            unsigned n;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) { continue; }
            if (dx == 0 && dy == 0) { continue; }
            n = ref[(size_t)ny * (size_t)w + (size_t)nx];
            /* The neighbour is compared with what the card produced. If they
               agree, this pixel took the surface next door — which is the whole
               of what a half-pixel shift can do. */
            if (channel_gap(dkr_image_to565(n & 0x00FFFFFFu), got) <=
                DKR_IMAGE_FRANK_GAP) {
                return 1;
            }
        }
    }
    return 0;
}

void dkr_image_compare(const unsigned *ref, const unsigned *got,
                       int w, int h, dkr_image_metrics *out)
{
    int x, y;

    if (!out) { return; }
    memset(out, 0, sizeof(*out));
    if (!ref || !got || w <= 0 || h <= 0) { return; }

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const size_t i = (size_t)y * (size_t)w + (size_t)x;
            const unsigned a = dkr_image_to565(ref[i] & 0x00FFFFFFu);
            const unsigned b = got[i] & 0x00FFFFFFu;
            const int gap = channel_gap(a, b);

            out->total++;
            if (gap > out->max_gap) { out->max_gap = gap; }
            if (a) { out->painted_ref++; }
            if (b) { out->painted_got++; }
            if (gap > DKR_IMAGE_FRANK_GAP) {
                if (is_edge(ref, w, h, x, y, b)) {
                    out->differ_edge++;
                } else {
                    out->differ++;
                    if (gap > out->worst) {
                        out->worst = gap;
                        out->worst_x = x;
                        out->worst_y = y;
                    }
                }
            }
        }
    }
}

void dkr_image_diff_map(const unsigned *ref, const unsigned *got,
                        int w, int h, unsigned *out)
{
    int x, y;
    if (!ref || !got || !out || w <= 0 || h <= 0) { return; }

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const size_t i = (size_t)y * (size_t)w + (size_t)x;
            const unsigned a = dkr_image_to565(ref[i] & 0x00FFFFFFu);
            const unsigned b = got[i] & 0x00FFFFFFu;
            const int gap = channel_gap(a, b);

            if (gap > DKR_IMAGE_FRANK_GAP && is_edge(ref, w, h, x, y, b)) {
                /* Forgiven as an edge, and shown as forgiven. A map that painted
                   these the same red as the rest would disagree with the number
                   printed beside it, and the reader would believe whichever they
                   looked at last. */
                out[i] = 0x604000u;
            } else if (gap <= DKR_IMAGE_FRANK_GAP) {
                /* The reference, darkened, so that the scene stays recognisable
                   underneath. A map on black tells you a pixel is wrong without
                   telling you what it is part of. */
                const unsigned r = ((a >> 16) & 0xFFu) / 5u;
                const unsigned g = ((a >>  8) & 0xFFu) / 5u;
                const unsigned bl = ( a        & 0xFFu) / 5u;
                out[i] = (r << 16) | (g << 8) | bl;
            } else {
                /* Red, scaled from the threshold to full: a gap of 25 and a gap
                   of 200 are both wrong, and only one of them is interesting. */
                int v = ((gap - DKR_IMAGE_FRANK_GAP) * 255) /
                        (255 - DKR_IMAGE_FRANK_GAP);
                if (v < 64) { v = 64; }
                if (v > 255) { v = 255; }
                out[i] = ((unsigned)v << 16);
            }
        }
    }
}

long dkr_image_per_million(long part, long total)
{
    if (total <= 0) { return 0; }
    /* In `double`, and then back. The values here are pixel counts, so the
       mantissa has room to spare; what there is no room for is the intermediate
       product in a 32-bit integer. */
    return (long)(((double)part * 1000000.0) / (double)total);
}

/* Little-endian, byte by byte. The header is written by one compiler and read by
   another, and a struct would carry whatever padding each of them chose. */
static void put_u32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)( v        & 0xFFu);
    p[1] = (unsigned char)((v >>  8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static unsigned int get_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned int get_u16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

int dkr_image_write_bmp(const char *path, const unsigned *pixels, int w, int h)
{
    FILE *f;
    const int pad = (4 - (w * 3) % 4) % 4;
    unsigned char head[54];
    unsigned data;
    int x, y, i;

    if (!path || !pixels || w <= 0 || h <= 0) { return 0; }
    data = (unsigned)((w * 3 + pad) * h);

    f = fopen(path, "wb");
    if (!f) { return 0; }

    memset(head, 0, sizeof(head));
    head[0] = 'B'; head[1] = 'M';
    put_u32(head + 2,  54u + data);
    put_u32(head + 10, 54u);
    put_u32(head + 14, 40u);
    put_u32(head + 18, (unsigned)w);
    put_u32(head + 22, (unsigned)h);   /* positive: rows bottom to top */
    head[26] = 1;                      /* planes */
    head[28] = 24;                     /* bits per pixel */
    put_u32(head + 34, data);
    if (fwrite(head, 1, sizeof(head), f) != sizeof(head)) {
        fclose(f);
        return 0;
    }
    /* BMP stores rows bottom to top and the buffer top to bottom, so the walk
       runs backwards. Getting this wrong gives a flipped image, which an
       automatic comparison reports as entirely different — a diagnosis far more
       expensive than the defect. */
    for (y = h - 1; y >= 0; y--) {
        for (x = 0; x < w; x++) {
            const unsigned c = pixels[(size_t)y * (size_t)w + (size_t)x];
            unsigned char bgr[3];
            bgr[0] = (unsigned char)( c        & 0xFFu);
            bgr[1] = (unsigned char)((c >>  8) & 0xFFu);
            bgr[2] = (unsigned char)((c >> 16) & 0xFFu);
            if (fwrite(bgr, 1, 3, f) != 3) { fclose(f); return 0; }
        }
        for (i = 0; i < pad; i++) {
            if (fputc(0, f) == EOF) { fclose(f); return 0; }
        }
    }
    /* Committed and then closed, both checked. `fclose` hands the bytes to
       Windows 95 and Windows 95 keeps them: an image written and then lost with
       the write-behind cache when the machine is stopped looks exactly like an
       image that was never written, and this target is normally stopped by a
       kill. Measured on the captures of 4 September 2026, five of six lost that
       way. */
#ifdef _WIN32
    if (fflush(f) != 0 || _commit(_fileno(f)) != 0) { fclose(f); return 0; }
#endif
    return fclose(f) == 0;
}

int dkr_image_read_bmp(const char *path, unsigned **pixels, int *w, int *h)
{
    unsigned char head[54];
    FILE *f;
    unsigned offset, width, height, bpp, planes, compression;
    int pad, x, y;
    unsigned *img;

    if (!path || !pixels || !w || !h) { return 0; }
    *pixels = 0;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "bmp: cannot open %s\n", path);
        return 0;
    }
    if (fread(head, 1, sizeof(head), f) != sizeof(head)) {
        fprintf(stderr, "bmp: %s is shorter than its header\n", path);
        fclose(f);
        return 0;
    }
    if (head[0] != 'B' || head[1] != 'M') {
        fprintf(stderr, "bmp: %s does not start with BM\n", path);
        fclose(f);
        return 0;
    }
    offset      = get_u32(head + 10);
    width       = get_u32(head + 18);
    height      = get_u32(head + 22);
    planes      = get_u16(head + 26);
    bpp         = get_u16(head + 28);
    compression = get_u32(head + 30);

    /* Only the one shape this project writes is accepted. A reader that guesses
       at the others would silently mis-decode a file some other tool produced,
       and the resulting image would be blamed on a renderer. */
    if (bpp != 24u || planes != 1u || compression != 0u) {
        fprintf(stderr, "bmp: %s is %u bits, %u planes, compression %u —"
                        " this reads 24-bit uncompressed only\n",
                path, bpp, planes, compression);
        fclose(f);
        return 0;
    }
    if ((int)height <= 0) {
        /* A negative height means rows stored top to bottom. Nothing here
           writes one, and reading it as bottom-up would flip the image. */
        fprintf(stderr, "bmp: %s is stored top-down, which this does not read\n",
                path);
        fclose(f);
        return 0;
    }
    if (width == 0u || width > 4096u || height > 4096u) {
        fprintf(stderr, "bmp: %s claims %ux%u\n", path, width, height);
        fclose(f);
        return 0;
    }

    img = (unsigned *)malloc((size_t)width * (size_t)height * sizeof(unsigned));
    if (!img) {
        fprintf(stderr, "bmp: cannot allocate %ux%u\n", width, height);
        fclose(f);
        return 0;
    }
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fprintf(stderr, "bmp: %s has no pixel data at offset %u\n", path, offset);
        free(img);
        fclose(f);
        return 0;
    }

    pad = (int)((4u - (width * 3u) % 4u) % 4u);
    for (y = (int)height - 1; y >= 0; y--) {
        for (x = 0; x < (int)width; x++) {
            unsigned char bgr[3];
            if (fread(bgr, 1, 3, f) != 3) {
                fprintf(stderr, "bmp: %s is truncated at row %d\n", path, y);
                free(img);
                fclose(f);
                return 0;
            }
            img[(size_t)y * (size_t)width + (size_t)x] =
                ((unsigned)bgr[2] << 16) | ((unsigned)bgr[1] << 8) | bgr[0];
        }
        if (pad && fseek(f, pad, SEEK_CUR) != 0) {
            fprintf(stderr, "bmp: %s is truncated in the padding of row %d\n",
                    path, y);
            free(img);
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    *pixels = img;
    *w = (int)width;
    *h = (int)height;
    return 1;
}
