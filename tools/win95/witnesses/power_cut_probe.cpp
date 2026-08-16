/* E02-S05 - the power cut, caused rather than simulated.
 *
 * What the durable-write sequence promises is not "the last write is never lost"
 * but **"a valid save is never lost"**. The window is accepted: between the rename
 * of SAVE.DAT to SAVE.BAK and that of SAVE.TMP to SAVE.DAT, the final file does
 * not exist.
 *
 * Until now the promise was reasoned about and tried by simulation. On an emulated
 * machine, the real cut is within reach: `kill -9` on the emulator carries off the
 * guest's disk cache exactly as a pulled plug would.
 *
 * Two modes:
 *
 *   write    an endless loop, each round writing a numbered save
 *   verify   reads back after a reboot and says what survived
 *
 * The content carries a round number and a checksum, so that "complete" is told
 * apart from "truncated". Without that we would not know whether the file found is
 * usable or merely present - and that is the whole difference the sequence claims
 * to guarantee.
 *
 * One reservation about the severity: `kill -9` also carries off what the emulator
 * held in the host's cache, which real hardware would already have written. The
 * protocol is therefore **at least as harsh** as a genuine cut, never gentler.
 * That is the right direction for the error to lie in.
 */
#include "fileio.h"

#include <stdio.h>
#include <string.h>

#define PAYLOAD 512

/* A simple checksum; the point is not to resist forgery but to tell a complete
   file from a truncated one. */
static unsigned long checksum(const unsigned char *p, size_t n)
{
    unsigned long sum = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        sum = (sum << 1) ^ (sum >> 31) ^ p[i];
    }
    return sum;
}

static void fill(unsigned char *buf, unsigned long round)
{
    size_t i;
    memset(buf, 0, PAYLOAD);
    /* The round number first, in explicit bytes: the probe must read itself back
       without depending on the compiler's endianness. */
    buf[0] = (unsigned char)(round & 0xFF);
    buf[1] = (unsigned char)((round >> 8) & 0xFF);
    buf[2] = (unsigned char)((round >> 16) & 0xFF);
    buf[3] = (unsigned char)((round >> 24) & 0xFF);
    for (i = 8; i < PAYLOAD; i++) {
        buf[i] = (unsigned char)((round + i) & 0xFF);
    }
    {
        const unsigned long c = checksum(buf + 8, PAYLOAD - 8);
        buf[4] = (unsigned char)(c & 0xFF);
        buf[5] = (unsigned char)((c >> 8) & 0xFF);
        buf[6] = (unsigned char)((c >> 16) & 0xFF);
        buf[7] = (unsigned char)((c >> 24) & 0xFF);
    }
}

static int payload_is_intact(const unsigned char *buf, size_t n,
                             unsigned long *round_out)
{
    unsigned long stored, computed;
    if (n != PAYLOAD) {
        return 0;
    }
    stored = (unsigned long)buf[4] | ((unsigned long)buf[5] << 8) |
             ((unsigned long)buf[6] << 16) | ((unsigned long)buf[7] << 24);
    computed = checksum(buf + 8, PAYLOAD - 8);
    if (round_out) {
        *round_out = (unsigned long)buf[0] | ((unsigned long)buf[1] << 8) |
                     ((unsigned long)buf[2] << 16) | ((unsigned long)buf[3] << 24);
    }
    return stored == computed;
}

int main(int argc, char **argv)
{
    const char *mode = (argc >= 2) ? argv[1] : "write";
    const char *path = (argc >= 3) ? argv[2] : "D:\\PWRCUT.DAT";
    unsigned char buf[PAYLOAD];

    if (strcmp(mode, "verify") == 0) {
        size_t          got = 0;
        int             from_backup = 0;
        unsigned long   round = 0;
        dkr_file_result r;
        FILE           *log = fopen("D:\\PWRCUT.TXT", "w");

        r = dkr_file_read_durable(path, buf, sizeof(buf), &got, &from_backup);

        {
            const int intact = (r == DKR_FILE_OK) &&
                               payload_is_intact(buf, got, &round);
            const char *verdict =
                (r != DKR_FILE_OK) ? "NO readable save"
                : intact ? (from_backup ? "valid save, from the backup copy"
                                        : "valid save, main file")
                         : "file present but TRUNCATED";
            printf("  read code          : %d (%s)\n", (int)r, dkr_file_result_text(r));
            printf("  bytes read back    : %u\n", (unsigned)got);
            printf("  round number       : %lu\n", round);
            printf("  verdict            : %s\n", verdict);
            if (log) {
                fprintf(log, "  read code          : %d (%s)\n",
                        (int)r, dkr_file_result_text(r));
                fprintf(log, "  bytes read back    : %u\n", (unsigned)got);
                fprintf(log, "  round number       : %lu\n", round);
                fprintf(log, "  verdict            : %s\n", verdict);
                fclose(log);
            }
            return intact ? 0 : 1;
        }
    }

    /* Write mode: endless, until the machine stops. */
    {
        unsigned long round = 0;
        printf("Writing in a loop to %s - cut the machine whenever you like.\n", path);
        for (;;) {
            fill(buf, round);
            if (dkr_file_write_durable(path, buf, sizeof(buf)) != DKR_FILE_OK) {
                printf("  failed at round %lu\n", round);
                return 2;
            }
            round++;
            if ((round % 25) == 0) {
                printf("  %lu rounds\n", round);
                fflush(stdout);
            }
        }
    }
}
