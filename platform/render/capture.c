/* E09-S02 — implementation. The contract and the reasoning live in `capture.h`. */

#include "capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>       /* _commit */
#endif

/* Pushes a file all the way to the disk, not merely out of the C library.
 *
 * **`fclose` is not enough on this target**, and the cost of assuming it was is
 * on record twice. `D:\REPLAY.TXT` came back at zero bytes after a fault that
 * happened well after its first line was flushed; and on 4 September 2026 a run
 * armed with six captures produced one file, the other five having been written,
 * closed, and then lost with the guest's write-behind cache when the emulator was
 * stopped. On a machine where the normal end of a run is a crash or a kill, a
 * capture that is not on the platter is not a capture. */
static int commit_to_disk(FILE *f)
{
#ifdef _WIN32
    if (fflush(f) != 0) { return 0; }
    if (_commit(_fileno(f)) != 0) { return 0; }
#else
    if (fflush(f) != 0) { return 0; }
#endif
    return 1;
}

/* Little-endian on both hosts, written byte by byte rather than by casting the
   struct. A `fwrite` of the struct would carry whatever padding the compiler
   chose, and the two ends of this format are built by two different compilers
   for two different targets. */
static void put_u32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static unsigned int get_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

int dkr_capture_write(const char *path, unsigned int data_ptr,
                      const void *rdram, unsigned int rdram_bytes,
                      int rdram_native, unsigned int list_index,
                      int screen_w, int screen_h)
{
    unsigned char head[32];
    FILE *out;
    size_t written;

    if (!path || !rdram || rdram_bytes == 0u) { return 0; }

    out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "[gfx] capture: cannot open %s\n", path);
        return 0;
    }

    memset(head, 0, sizeof(head));
    put_u32(head + 0,  DKR_CAPTURE_MAGIC);
    put_u32(head + 4,  DKR_CAPTURE_VERSION);
    put_u32(head + 8,  data_ptr);
    put_u32(head + 12, rdram_bytes);
    put_u32(head + 16, rdram_native ? 1u : 0u);
    put_u32(head + 20, list_index);
    put_u32(head + 24, (unsigned int)screen_w);
    put_u32(head + 28, (unsigned int)screen_h);

    if (fwrite(head, 1, sizeof(head), out) != sizeof(head)) {
        fprintf(stderr, "[gfx] capture: header write failed\n");
        fclose(out);
        return 0;
    }

    /* **In chunks, and the short count is checked.** Eight mebibytes in one
       `fwrite` to an emulated disk is where a partial write would go unnoticed:
       the call returns a count, nobody looks at it, and the capture replays a
       truncated RDRAM as a rendering defect. */
    {
        const unsigned char *p = (const unsigned char *)rdram;
        unsigned int left = rdram_bytes;
        while (left > 0u) {
            const unsigned int chunk = (left > 65536u) ? 65536u : left;
            written = fwrite(p, 1, chunk, out);
            if (written != (size_t)chunk) {
                fprintf(stderr,
                        "[gfx] capture: short write, %u of %u bytes left\n",
                        left, rdram_bytes);
                fclose(out);
                return 0;
            }
            p += chunk;
            left -= chunk;
        }
    }

    /* Committed before the close, and the close checked after it. Windows 95
       reports a full disk at one or the other and not earlier -- the write-behind
       cache accepts what it cannot store -- and a capture that failed at either
       is not a capture. */
    if (!commit_to_disk(out)) {
        fprintf(stderr, "[gfx] capture: %s could not be committed to disk\n",
                path);
        fclose(out);
        return 0;
    }
    if (fclose(out) != 0) {
        fprintf(stderr, "[gfx] capture: close failed for %s\n", path);
        return 0;
    }
    fprintf(stderr, "[gfx] capture: %s list=%u data=0x%06X rdram=%u native=%d\n",
            path, list_index, data_ptr, rdram_bytes, rdram_native ? 1 : 0);
    return 1;
}

int dkr_capture_read(const char *path, dkr_capture_header *out,
                     unsigned char **rdram)
{
    unsigned char head[32];
    FILE *in;

    if (!path || !out || !rdram) { return 0; }
    *rdram = 0;

    in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "capture: cannot open %s\n", path);
        return 0;
    }
    if (fread(head, 1, sizeof(head), in) != sizeof(head)) {
        fprintf(stderr, "capture: %s is shorter than its header\n", path);
        fclose(in);
        return 0;
    }

    out->magic        = get_u32(head + 0);
    out->version      = get_u32(head + 4);
    out->data_ptr     = get_u32(head + 8);
    out->rdram_bytes  = get_u32(head + 12);
    out->rdram_native = get_u32(head + 16);
    out->list_index   = get_u32(head + 20);
    out->screen_w     = get_u32(head + 24);
    out->screen_h     = get_u32(head + 28);

    /* Each refusal names itself. "capture: invalid" would leave the reader
       guessing between a wrong file, an old format and a truncated transfer --
       three problems with three different answers. */
    if (out->magic != DKR_CAPTURE_MAGIC) {
        fprintf(stderr, "capture: %s is not a capture (magic %08X)\n",
                path, out->magic);
        fclose(in);
        return 0;
    }
    if (out->version != DKR_CAPTURE_VERSION) {
        fprintf(stderr, "capture: %s is version %u, this build reads %u\n",
                path, out->version, (unsigned)DKR_CAPTURE_VERSION);
        fclose(in);
        return 0;
    }
    if (out->rdram_bytes == 0u || out->rdram_bytes > 64u * 1024u * 1024u) {
        fprintf(stderr, "capture: %s claims %u bytes of RDRAM\n",
                path, out->rdram_bytes);
        fclose(in);
        return 0;
    }

    *rdram = (unsigned char *)malloc(out->rdram_bytes);
    if (!*rdram) {
        fprintf(stderr, "capture: cannot allocate %u bytes\n", out->rdram_bytes);
        fclose(in);
        return 0;
    }
    if (fread(*rdram, 1, out->rdram_bytes, in) != (size_t)out->rdram_bytes) {
        /* The check that matters most. A transfer that dropped the tail gives a
           capture whose header is perfect and whose geometry stops halfway, and
           the resulting image looks exactly like a decoder that gives up early. */
        fprintf(stderr, "capture: %s is truncated, wanted %u bytes of RDRAM\n",
                path, out->rdram_bytes);
        free(*rdram);
        *rdram = 0;
        fclose(in);
        return 0;
    }
    fclose(in);
    return 1;
}
