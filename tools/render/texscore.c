/* Scoring a texture-conversion rule, on the host.
 *
 *   texscore <baseline-dir> <candidate-dir> [--threshold PCT] [--quiet]
 *
 * Both directories hold what `replay --dump-textures` wrote for the same
 * capture, once with a conversion rule off and once with it on. The tool pairs
 * the dumps up, measures each one, and says how many the rule improved, how many
 * it damaged, and which.
 *
 * **Why this exists as a program.** The odd-row swap and `--row-from-line` were
 * both decided on a table of "smoother / rougher / unchanged" over several
 * scenes, and that table was produced by a script that lived in a temporary
 * directory and no longer exists. A measurement that decided what ships on the
 * default path, and that nobody can run again, is an assertion. This is the same
 * measurement, in the tree, with its metric written down.
 *
 * ## The metric
 *
 * **Roughness is the mean absolute difference between adjacent texels**, over
 * the four channels, counted horizontally and vertically and averaged over both
 * directions. An image whose texels lie in the order they were meant to lie in
 * is locally smooth; one read at the wrong pitch, or with every other row's
 * texels exchanged in pairs, is not.
 *
 * The vertical term is the half that matters for the defects this port keeps
 * meeting, and the earlier scoring did not have it. The RDP's odd-row swap
 * touches *odd rows only*: it leaves each row's own neighbours largely intact
 * and sets consecutive rows against each other. A horizontal-only score sees
 * that far more faintly than the eye does.
 *
 * ## Pairing, and why by slot
 *
 * By the oracle's texture slot, which `dump_textures` puts at the head of every
 * name as `tex%03d_`. Not by the key in the name: a rule that changes the row
 * length changes the width, the width is part of the key, and a texture would
 * stop matching itself precisely when the rule did something. The upload order
 * is a property of the display list and no conversion rule moves it, so the slot
 * is the same texture on both sides by construction.
 *
 * A slot present on one side only is reported as such rather than skipped: it
 * means the two runs decoded different lists, and every number after it is worth
 * nothing.
 */

#include "render/imagecmp.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXSCORE_SLOTS 512

typedef struct {
    char  name[256];
    int   have;
} texscore_entry;

/* The roughness of one image, in texel levels. Zero for an image with no pair of
   adjacent texels in either direction, which is a 1x1 and carries no layout to
   get wrong. */
static double texscore_roughness(const unsigned *px, int w, int h)
{
    double sum = 0.0;
    long   pairs = 0;
    int    x, y, c;

    for (y = 0; y < h; y++) {
        for (x = 0; x + 1 < w; x++) {
            const unsigned a = px[(size_t)y * (size_t)w + (size_t)x];
            const unsigned b = px[(size_t)y * (size_t)w + (size_t)x + 1];
            for (c = 0; c < 4; c++) {
                const int av = (int)((a >> (c * 8)) & 0xFFu);
                const int bv = (int)((b >> (c * 8)) & 0xFFu);
                sum += (av > bv) ? (av - bv) : (bv - av);
            }
            pairs += 4;
        }
    }
    for (y = 0; y + 1 < h; y++) {
        for (x = 0; x < w; x++) {
            const unsigned a = px[(size_t)y * (size_t)w + (size_t)x];
            const unsigned b = px[(size_t)(y + 1) * (size_t)w + (size_t)x];
            for (c = 0; c < 4; c++) {
                const int av = (int)((a >> (c * 8)) & 0xFFu);
                const int bv = (int)((b >> (c * 8)) & 0xFFu);
                sum += (av > bv) ? (av - bv) : (bv - av);
            }
            pairs += 4;
        }
    }
    if (pairs == 0) { return 0.0; }
    return sum / (double)pairs;
}

/* `tex007_16x15_of_16x16_at32D9E0_...` -> 7, or -1 if the name is not one of
   ours. Nothing else in the directory is read, so a stray file is ignored rather
   than counted as a texture. */
static int texscore_slot_of(const char *name)
{
    int slot = 0, digits = 0;
    if (strncmp(name, "tex", 3) != 0) { return -1; }
    name += 3;
    while (*name >= '0' && *name <= '9') {
        slot = slot * 10 + (*name - '0');
        name++;
        digits++;
    }
    if (digits == 0 || *name != '_') { return -1; }
    return slot;
}

static int texscore_collect(const char *dir, texscore_entry *into)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;
    if (!d) {
        fprintf(stderr, "texscore: cannot open %s\n", dir);
        return -1;
    }
    while ((e = readdir(d)) != 0) {
        const int slot = texscore_slot_of(e->d_name);
        if (slot < 0 || slot >= TEXSCORE_SLOTS) { continue; }
        if (into[slot].have) {
            fprintf(stderr, "texscore: two files for slot %d in %s\n", slot, dir);
            closedir(d);
            return -1;
        }
        if (strlen(e->d_name) >= sizeof(into[slot].name)) {
            fprintf(stderr, "texscore: name too long: %s\n", e->d_name);
            closedir(d);
            return -1;
        }
        strcpy(into[slot].name, e->d_name);
        into[slot].have = 1;
        found++;
    }
    closedir(d);
    return found;
}

static int texscore_read(const char *dir, const char *name,
                         unsigned **px, int *w, int *h)
{
    char path[1024];
    if (strlen(dir) + strlen(name) + 2 > sizeof(path)) { return 0; }
    sprintf(path, "%s/%s", dir, name);
    return dkr_image_read_bmp(path, px, w, h);
}

int main(int argc, char **argv)
{
    static texscore_entry base[TEXSCORE_SLOTS], cand[TEXSCORE_SLOTS];
    const char *base_dir = 0, *cand_dir = 0;
    /* One per cent, not the twenty the pitch probe used. Twenty per cent was a
       decision threshold — "is this texture better read the other way" — and
       this is a census: a texture the rule moved at all belongs in one of the
       two columns, and the column totals are what the tables report. */
    double threshold = 1.0;
    int quiet = 0;
    int i, slot;
    int smoother = 0, rougher = 0, unchanged = 0, paired = 0, orphans = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--threshold") == 0 && i + 1 < argc) {
            threshold = atof(argv[++i]);
        } else if (strcmp(argv[i], "--quiet") == 0) {
            quiet = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "texscore: unknown option %s\n", argv[i]);
            return 2;
        } else if (!base_dir) { base_dir = argv[i];
        } else if (!cand_dir) { cand_dir = argv[i];
        } else {
            fprintf(stderr, "texscore: too many directories\n");
            return 2;
        }
    }
    if (!base_dir || !cand_dir) {
        fprintf(stderr,
                "usage: texscore <baseline-dir> <candidate-dir>"
                " [--threshold PCT] [--quiet]\n");
        return 2;
    }

    if (texscore_collect(base_dir, base) < 0) { return 2; }
    if (texscore_collect(cand_dir, cand) < 0) { return 2; }

    for (slot = 0; slot < TEXSCORE_SLOTS; slot++) {
        unsigned *bp = 0, *cp = 0;
        int bw = 0, bh = 0, cw = 0, ch = 0;
        double br, cr, delta;

        if (!base[slot].have && !cand[slot].have) { continue; }
        if (!base[slot].have || !cand[slot].have) {
            printf("  slot %3d  only in %s  %s\n", slot,
                   base[slot].have ? "baseline" : "candidate",
                   base[slot].have ? base[slot].name : cand[slot].name);
            orphans++;
            continue;
        }
        if (!texscore_read(base_dir, base[slot].name, &bp, &bw, &bh) ||
            !texscore_read(cand_dir, cand[slot].name, &cp, &cw, &ch)) {
            free(bp);
            free(cp);
            return 2;
        }
        br = texscore_roughness(bp, bw, bh);
        cr = texscore_roughness(cp, cw, ch);
        free(bp);
        free(cp);
        paired++;

        /* Relative to the baseline, because roughness has no absolute scale: a
           dark texture and a bright one are not comparable in levels, and only
           the change within one texture is. A baseline of zero is flat, and a
           flat texture cannot be improved. */
        if (br <= 0.0) { unchanged++; continue; }
        delta = (cr - br) * 100.0 / br;
        if (delta < -threshold) {
            smoother++;
            if (!quiet) {
                printf("  smoother %+7.1f%%  slot %3d  %6.2f -> %6.2f  %s\n",
                       delta, slot, br, cr, cand[slot].name);
            }
        } else if (delta > threshold) {
            rougher++;
            if (!quiet) {
                printf("  ROUGHER  %+7.1f%%  slot %3d  %6.2f -> %6.2f  %s\n",
                       delta, slot, br, cr, cand[slot].name);
            }
        } else {
            unchanged++;
        }
    }

    printf("textures %d  smoother %d  rougher %d  unchanged %d\n",
           paired, smoother, rougher, unchanged);
    if (orphans != 0) {
        printf("WARNING: %d slot(s) on one side only -- the two runs did not"
               " decode the same list\n", orphans);
        return 1;
    }
    return 0;
}
