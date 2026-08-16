/* E02-S05 - is a save produced here readable elsewhere?
 *
 * The acceptance criterion asks for interchange in both directions between the
 * modern build and the Windows 95 one. This witness is the *producing* half of the
 * trial: it writes a non-trivial adventure save, and the same source is compiled
 * for both targets.
 *
 * The *consuming* half already exists: `dkr_save_codec_tests` takes a file as an
 * argument, decodes it, re-encodes it, and demands equality **byte for byte**.
 * That is the right demand, and it is stronger than a mere "the decode succeeds":
 * it also catches encoding differences, which are precisely what a change of
 * platform risks introducing - endianness, padding, field widths.
 *
 * The content written is not a blank save. A blank save is mostly made of zeros,
 * and zeros survive just about any conversion error. So we lay distinct values in
 * fields of differing widths, so that an error shows.
 */
#include "dkr_save_codec.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    using namespace dkr::runtime::saves::codec;

    const char *path = (argc >= 2) ? argv[1] : "SAVEOUT.BIN";

    SaveImage image{};
    std::string error;
    const std::vector<std::uint8_t> blank = blank_bytes();
    if (!decode(blank, image, &error)) {
        std::fprintf(stderr, "[interchange] the blank save does not decode: %s\n",
                     error.c_str());
        return 1;
    }

    /* Values chosen so that a conversion error shows.
     *
     * The 16- and 32-bit fields carry **asymmetric** patterns - 0x1234 and not
     * 0x1221 - because a byte swap on a symmetric value does not show. That is the
     * main risk when the same structure is encoded by two different compilers, and
     * the only one that zeros never reveal. */
    image.slots[0].name             = "WIN95";
    image.slots[0].course_status[0] = 3;
    image.slots[0].balloons[0]      = 37;
    image.slots[0].balloons[1]      = 8;
    image.slots[0].wizpig_amulet    = 4;
    image.slots[0].tt_amulet        = 1;
    image.slots[0].trophies         = 0x1234;
    image.slots[0].bosses           = 0x5678;
    image.slots[0].world_flags[0]   = 0x0BAD;
    image.slots[0].cutscenes        = 0x12345678u;
    image.slots[0].keys             = 5;
    image.slots[1].balloons[0]      = 12;

    const std::vector<std::uint8_t> bytes = encode(image);

    FILE *out = std::fopen(path, "wb");
    if (!out) {
        std::fprintf(stderr, "[interchange] cannot write: %s\n", path);
        return 1;
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), out);
    /* On a full medium, `fwrite` may return less than asked for without `fclose`
       saying so. The count, on the other hand, does not lie. */
    const bool closed_cleanly = (std::fclose(out) == 0);
    if (written != bytes.size() || !closed_cleanly) {
        std::fprintf(stderr, "[interchange] incomplete write: %u of %u bytes\n",
                     (unsigned)written, (unsigned)bytes.size());
        return 1;
    }

    std::printf("[interchange] wrote %u bytes to %s\n",
                (unsigned)bytes.size(), path);
    return 0;
}
