// E03-S03 - one audio command at a time, the high-level mixer against the
// recompiled microcode.
//
// Each case is a synthetic task: fill the whole buffer area (DMEM 0x5C0, 0xA00
// bytes) from random RDRAM, run the command under test with random parameters,
// then save the whole area back to RDRAM. The same task goes through
// `dkrAspMain` (the oracle, SIMD) and `dkr_aspmain_hle`, and everything is
// compared: the saved area, any state the command wrote to RDRAM, and the state
// block at DMEM 0x360.
//
//   abi_difftest <capture> [command [cases]]
//
// The capture only lends its DMEM, for the microcode's data and tables.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "librecomp/rsp.hpp"
extern "C" {
#include "aspmain_hle.h"
}

uint8_t dmem[0x1000];
RspExitReason dkrAspMain(uint8_t* rdram, uint32_t ucode_addr);

namespace {

constexpr uint32_t kRdram = 0x800000u;
constexpr uint32_t kSource = 0x100000u;   // random data loaded into the buffers
constexpr uint32_t kList = 0x200000u;     // the command list
constexpr uint32_t kSaved = 0x300000u;    // where the buffer area is saved
constexpr uint32_t kState = 0x310000u;    // state saved by ADPCM, RESAMPLE...
constexpr uint32_t kTable = 0x320000u;    // codebooks
constexpr uint32_t kArea = 0xA00u;

uint32_t rng_state = 0xC0FFEE11u;
uint32_t g_resample_remainder = 0;
uint32_t rng() {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}

void put8(std::vector<uint8_t>& m, uint32_t a, uint8_t v) { m[a ^ 3] = v; }
void put32(std::vector<uint8_t>& m, uint32_t a, uint32_t v) {
    put8(m, a, v >> 24); put8(m, a + 1, v >> 16); put8(m, a + 2, v >> 8); put8(m, a + 3, v);
}

struct List {
    std::vector<uint32_t> words;
    void add(uint32_t w0, uint32_t w1) { words.push_back(w0); words.push_back(w1); }
};
uint32_t cmd(uint32_t op, uint32_t flags, uint32_t low) { return (op << 24) | (flags << 16) | (low & 0xFFFF); }
enum { SPNOOP, ADPCM, CLEARBUFF, ENVMIXER, LOADBUFF, RESAMPLE, SAVEBUFF, SEGMENT,
       SETBUFF, SETVOL, DMEMMOVE, LOADADPCM, MIXER, INTERLEAVE, POLEF, SETLOOP };

// Buffer offsets the tests use: even, inside the area, leaving room for count.
uint32_t offset(uint32_t count) { return (rng() % ((kArea - count) / 16)) * 16; }

void add_test(const std::string& name, List& l) {
    if (name == "clearbuff") {
        const uint32_t count = (rng() % 0x200) & ~1u;
        l.add(cmd(CLEARBUFF, 0, offset(0x210)), count);
    } else if (name == "dmemmove") {
        const uint32_t count = (rng() % 0x200) & ~1u;
        l.add(cmd(DMEMMOVE, 0, offset(0x210)), (offset(0x210) << 16) | count);
    } else if (name == "mixer") {
        const uint32_t count = ((rng() % 0x1C0) + 2) & ~1u;
        // As the game uses it: in place (a gain on a buffer), or between two
        // disjoint buffers. The microcode loads the next block before it stores
        // the last, so a partial overlap -- which no DKR list contains -- would
        // come out differently from any sequential implementation.
        l.add(cmd(SETBUFF, 0, 0), count);
        const uint32_t in = (rng() % 0x10) * 16, out = (rng() & 1) ? in : 0x500 + (rng() % 0x10) * 16;
        l.add(cmd(MIXER, 0, rng()), (in << 16) | out);
    } else if (name == "interleave") {
        const uint32_t count = ((rng() % 0x100) + 2) & ~1u;
        // As the game uses it: the output below both inputs, all three disjoint.
        l.add(cmd(SETBUFF, 0, 0), (((rng() % 0x10) * 16) << 16) | count);
        l.add(cmd(INTERLEAVE, 0, 0), ((0x400 + (rng() % 0x10) * 16) << 16) | (0x600 + (rng() % 0x10) * 16));
    } else if (name == "setvol") {
        const uint32_t flags = rng() & 0x0E;
        l.add(cmd(SETVOL, flags, rng()), rng());
    } else if (name == "loadsave") {
        const uint32_t count = ((rng() % 0x200) + 1);
        l.add(cmd(SETBUFF, 0, offset(0x210)), (offset(0x210) << 16) | count);
        l.add(cmd(LOADBUFF, 0, 0), kSource + (rng() % 0x400));
        l.add(cmd(SAVEBUFF, 0, 0), kState + (rng() % 0x400));
    } else if (name == "adpcm") {
        // A random codebook of 16 predictors, random frames, and each of the
        // three ways the state is found: A_INIT, from the address, from the loop.
        l.add(cmd(LOADADPCM, 0, 0x100), kTable);
        const uint32_t count = ((rng() % 12) + 1) * 32 - ((rng() & 1) ? (rng() % 32) & ~1u : 0);
        const uint32_t in = 0x400 + (rng() % 8) * 16;
        l.add(cmd(SETBUFF, 0, in), ((rng() % 8) * 16 << 16) | count);
        const uint32_t mode = rng() % 3;
        if (mode == 2) { l.add(cmd(SETLOOP, 0, 0), kState + 0x40); }
        l.add(cmd(ADPCM, mode == 0 ? 1 : (mode == 2 ? 2 : 0), 0), kState);
    } else if (name == "resample") {
        // Pitches from far below to near the ceiling of 1.99996, the three
        // state modes (init, from RDRAM, flag 2), and counts that are and are
        // not multiples of eight outputs.
        const uint32_t count = ((rng() % 0x100) + 2) & ~1u;
        const uint32_t in = 0x300 + (rng() % 0x10) * 16;
        l.add(cmd(SETBUFF, 0, in), (((rng() % 0x10) * 16) << 16) | count);
        const uint32_t pitch = (rng() & 3) == 0 ? 0x8000 : (rng() % 0xFFFF) + 1;
        const uint32_t flags = (rng() % 3 == 0) ? 1 : ((rng() % 4 == 0) ? 2 : 0);
        l.add(cmd(RESAMPLE, flags, pitch), kState);
        g_resample_remainder = (rng() % 8) * 2;   // a real state's remainder: even, below 16
    } else if (name == "polef") {
        // The filter table is loaded like a codebook; gains of both signs.
        l.add(cmd(LOADADPCM, 0, 0x20), kTable);
        const uint32_t count = ((rng() % 0x100) + 2) & ~1u;
        l.add(cmd(SETBUFF, 0, 0x300 + (rng() % 0x10) * 16), (((rng() % 0x10) * 16) << 16) | count);
        l.add(cmd(POLEF, (rng() % 3 == 0) ? 1 : 0, rng()), kState);
    } else if (name == "envmixer") {
        // SETVOL for both sides (current, target and rate) and dry/wet, the
        // buffers as the game lays them out (all disjoint), with and without
        // A_AUX, and A_INIT or a state from RDRAM.
        const uint32_t count = ((rng() % 0x100) + 2) & ~1u;
        l.add(cmd(SETVOL, 0x06, rng()), 0);                   // A_VOL | A_LEFT
        l.add(cmd(SETVOL, 0x04, rng()), 0);                   // A_VOL | A_RIGHT
        // Half the time a side's rate is zero, as in half of DKR's calls: the
        // mixer then computes that side's gains once per call.
        l.add(cmd(SETVOL, 0x02, rng()), (rng() & 1) ? 0 : rng());   // A_RATE | A_LEFT
        l.add(cmd(SETVOL, 0x00, rng()), (rng() & 1) ? 0 : rng());   // A_RATE | A_RIGHT
        l.add(cmd(SETVOL, 0x08, rng()), rng());               // A_AUX: dry, wet
        l.add(cmd(SETBUFF, 0, 0x000), (0x100u << 16) | count);
        l.add(cmd(SETBUFF, 0x08, 0x200), (0x300u << 16) | 0x400u);
        const uint32_t flags = ((rng() & 1) ? 1 : 0) | ((rng() & 1) ? 8 : 0);
        l.add(cmd(ENVMIXER, flags, 0), kState);
    } else if (name == "segment") {
        l.add(cmd(SEGMENT, 0, 0), (3u << 24) | kSource);
        l.add(cmd(SETBUFF, 0, 0), (offset(0x110) << 16) | 0x100);
        l.add(cmd(LOADBUFF, 0, 0), (3u << 24) | (rng() % 0x400));
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: abi_difftest <capture> [command [cases]]\n"); return 2; }
    std::vector<uint8_t> template_dmem(0x1000);
    uint32_t ucode = 0;
    {
        std::FILE* f = std::fopen(argv[1], "rb");
        if (!f) { std::perror(argv[1]); return 2; }
        uint32_t h[4];
        if (std::fread(h, sizeof(h), 1, f) != 1 ||
            std::fread(template_dmem.data(), 1, 0x1000, f) != 0x1000) { return 2; }
        ucode = h[2];
        std::fclose(f);
    }
    const std::vector<std::string> all = {"clearbuff", "dmemmove", "mixer", "interleave",
                                          "setvol", "loadsave", "segment", "adpcm", "resample", "polef", "envmixer"};
    std::vector<std::string> names = all;
    if (argc >= 3) { names = {argv[2]}; }
    const int cases = argc >= 4 ? std::atoi(argv[3]) : 200;
    int failing = 0;

    std::vector<uint8_t> a(kRdram), b(kRdram);
    for (const auto& name : names) {
        int bad = 0;
        for (int c = 0; c < cases; c++) {
            std::fill(a.begin(), a.end(), 0);
            for (uint32_t i = 0; i < 0x800; i++) { put8(a, kSource + i, rng()); }
            for (uint32_t i = 0; i < 0x100; i++) { put8(a, kTable + i, rng()); }
            for (uint32_t i = 0; i < 0x80; i++) { put8(a, kState + i, rng()); }
            List l;
            l.add(cmd(SETBUFF, 0, 0), (0u << 16) | kArea);
            l.add(cmd(LOADBUFF, 0, 0), kSource);
            add_test(name, l);
            l.add(cmd(SETBUFF, 0, 0), (0u << 16) | kArea);
            l.add(cmd(SAVEBUFF, 0, 0), kSaved);
            for (size_t i = 0; i < l.words.size(); i++) { put32(a, kList + 4 * i, l.words[i]); }
            if (name == "resample") {
                put8(a, kState + 0xA, 0); put8(a, kState + 0xB, g_resample_remainder);
            }
            b = a;

            std::vector<uint8_t> da = template_dmem;
            const uint32_t ptr = kList, size = static_cast<uint32_t>(l.words.size() * 4);
            std::memcpy(&da[0xFC0 + 0x30], &ptr, 4);
            std::memcpy(&da[0xFC0 + 0x34], &size, 4);
            std::vector<uint8_t> db = da;

            if (std::getenv("ABI_TRACE") != nullptr) {
                const size_t n = l.words.size();
                std::printf("case %d: %08X %08X after %08X %08X\n", c, l.words[n - 6], l.words[n - 5],
                            l.words[n - 8], l.words[n - 7]);
                std::fflush(stdout);
            }
            std::memcpy(dmem, da.data(), 0x1000);
            dkrAspMain(a.data(), ucode);
            std::memcpy(da.data(), dmem, 0x1000);
            dkr_aspmain_hle(b.data(), db.data());

            uint32_t first = 0; unsigned long diff = 0;
            for (uint32_t i = 0; i < kRdram; i++) {
                if (a[i] != b[i]) { if (!diff) first = i ^ 3; diff++; }
            }
            unsigned long state_diff = 0;
            for (uint32_t i = 0x360; i < 0x380; i++) { if (da[i] != db[i]) state_diff++; }
            if (diff || state_diff) {
                if (bad < 3) {
                    const size_t n = l.words.size();
                    std::printf("  %s case %d: %lu RDRAM bytes differ (first 0x%06X), %lu state bytes;"
                                " command %08X %08X after %08X %08X\n", name.c_str(), c, diff, first,
                                state_diff, l.words[n - 6], l.words[n - 5], l.words[n - 8], l.words[n - 7]);
                    if (std::getenv("ABI_DEBUG") != nullptr) {
                        if (name == "adpcm") {
                            // The frame the command decoded, and the codebook it used.
                            const uint32_t in = l.words[l.words.size() - 7 - ((l.words[l.words.size()-8] >> 24) == SETLOOP ? 2 : 0)];
                            (void)in;
                            std::printf("    first frame bytes (saved area copy of input):");
                            for (uint32_t i = 0; i < 0x40; i++) { std::printf("%s%02X", (i % 9) ? "" : " ", a[(kSaved + 0x400 + i) ^ 3]); }
                            std::printf("\n");
                        }
                        // Differing halfwords of the saved area: offset, oracle, mixer.
                        int shown = 0;
                        for (uint32_t off = 0; off < kArea && shown < 12; off += 2) {
                            const uint32_t p = kSaved + off;
                            const int16_t va = (int16_t)((a[p ^ 3] << 8) | a[(p + 1) ^ 3]);
                            const int16_t vb = (int16_t)((b[p ^ 3] << 8) | b[(p + 1) ^ 3]);
                            if (va != vb) { std::printf("    +0x%03X oracle %6d mixer %6d\n", off, va, vb); shown++; }
                        }
                    }
                }
                bad++;
            }
        }
        std::printf("%-11s %s (%d of %d cases differ)\n", name.c_str(), bad ? "FAIL" : "ok", bad, cases);
        if (bad) failing++;
    }
    return failing ? 1 : 0;
}
