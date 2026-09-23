// E03-S03 - replays captured DKR audio tasks through the high-level mixer and
// through the recompiled microcode (SIMD), and compares all of RDRAM and the
// buffer area of DMEM. The end-to-end test: real command lists, real samples,
// real state carried from task to task by the game.
//
//   replay_hle AUD000.BIN [...]
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "librecomp/rsp.hpp"
extern "C" {
#include "aspmain_hle.h"
}

uint8_t dmem[0x1000];
RspExitReason dkrAspMain(uint8_t* rdram, uint32_t ucode_addr);

int main(int argc, char** argv) {
    int failures = 0;
    for (int i = 1; i < argc; i++) {
        std::FILE* f = std::fopen(argv[i], "rb");
        if (!f) { std::perror(argv[i]); failures++; continue; }
        uint32_t h[4];
        std::vector<uint8_t> dm(0x1000);
        if (std::fread(h, sizeof(h), 1, f) != 1) { failures++; continue; }
        std::vector<uint8_t> rd(h[3]);
        if (std::fread(dm.data(), 1, 0x1000, f) != 0x1000 ||
            std::fread(rd.data(), 1, h[3], f) != h[3]) { failures++; continue; }
        std::fclose(f);

        std::vector<uint8_t> a(0x800000, 0), b(0x800000, 0);
        std::memcpy(a.data(), rd.data(), rd.size());
        std::memcpy(b.data(), rd.data(), rd.size());
        std::vector<uint8_t> db = dm;

        std::memcpy(dmem, dm.data(), 0x1000);
        dkrAspMain(a.data(), h[2]);
        const unsigned long commands = dkr_aspmain_hle(b.data(), db.data());

        unsigned long written = 0, rdram_diff = 0, dmem_diff = 0;
        uint32_t first = 0;
        for (uint32_t k = 0; k < a.size(); k++) {
            if (a[k] != (k < rd.size() ? rd[k] : 0)) written++;
            if (a[k] != b[k]) { if (!rdram_diff) first = k ^ 3; rdram_diff++; }
        }
        for (uint32_t k = 0x5C0; k < 0xFB0; k++) {
            if (dmem[k] != db[k]) dmem_diff++;
        }
        std::printf("%s: %lu commands, oracle wrote %lu RDRAM bytes; mixer differs on %lu RDRAM",
                    argv[i], commands, written, rdram_diff);
        if (rdram_diff) std::printf(" (first 0x%06X)", first);
        std::printf(" and %lu buffer bytes\n", dmem_diff);
        if (rdram_diff || dmem_diff) failures++;
    }
    return failures ? 1 : 0;
}
