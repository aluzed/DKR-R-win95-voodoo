/* E02-S05 - what does the durable write return when the medium is full?
 *
 * The ticket asked for distinct, usable error codes: "disk full" and "medium
 * write-protected" do not call for the same action from the player. The trial
 * suite so far checked that the codes are *distinct* and carry a text - which is
 * necessary and not sufficient. Nothing proved that a genuinely full disk returns
 * `DKR_FILE_ERR_NO_SPACE` rather than `DKR_FILE_ERR_IO`.
 *
 * That is what this probe establishes, and it needs a volume one can fill: a
 * 1.44 MB floppy mounted as A:, filled in advance from the host. The transfer hard
 * disk has half a gigabyte free, which makes the exercise impractical by that
 * route.
 *
 * Two precautions are worth stating:
 *
 *   - The write attempted is **larger than the whole volume**, and not merely than
 *     the space remaining. A write that only just fitted would prove nothing
 *     reproducible: the free space depends on whatever is lying around on the
 *     medium.
 *
 *   - The returned code is printed **with its text**, because it is the text the
 *     player will read. A correct code with a wrong message would be an illusory
 *     advance.
 */
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *path = (argc >= 2) ? argv[1] : "A:\\FULL.DAT";
    /* 2 MB: beyond the capacity of a 3.5" high-density floppy, whatever it already
       contains. */
    const size_t size = 2u * 1024u * 1024u;
    unsigned char *buffer;
    dkr_file_result r;
    FILE *log;

    log = fopen("D:\\DISKFULL.TXT", "w");

    buffer = (unsigned char *)malloc(size);
    if (!buffer) {
        printf("  not enough memory for the probe\n");
        if (log) { fprintf(log, "  not enough memory for the probe\n"); fclose(log); }
        return 2;
    }
    memset(buffer, 0xA5, size);

    r = dkr_file_write_durable(path, buffer, size);
    free(buffer);

    printf("  target             : %s\n", path);
    printf("  size requested     : %u bytes\n", (unsigned)size);
    printf("  code returned      : %d\n", (int)r);
    printf("  text               : %s\n", dkr_file_result_text(r));
    printf("  verdict            : %s\n",
           (r == DKR_FILE_ERR_NO_SPACE) ? "DISK FULL, correctly named"
                                        : "NOT the disk-full code");
    if (log) {
        fprintf(log, "  target             : %s\n", path);
        fprintf(log, "  size requested     : %u bytes\n", (unsigned)size);
        fprintf(log, "  code returned      : %d\n", (int)r);
        fprintf(log, "  text               : %s\n", dkr_file_result_text(r));
        fprintf(log, "  verdict            : %s\n",
                (r == DKR_FILE_ERR_NO_SPACE) ? "DISK FULL, correctly named"
                                             : "NOT the disk-full code");
        fclose(log);
    }
    return (r == DKR_FILE_ERR_NO_SPACE) ? 0 : 1;
}
