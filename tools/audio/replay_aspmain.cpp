// E03-S03 - replays captured DKR audio tasks through the recompiled microcode.
//
// A capture (`DKR_AUDIO_CAPTURE`, game_main.cpp) holds DMEM and the low four
// megabytes of RDRAM before and after the target ran the task. This loads the
// "before" half, runs the same `dkrAspMain` on the host, and compares every
// byte of RDRAM with the "after" half. A match says the host is a faithful
// oracle for the high-level mixer to be measured against; a mismatch says the
// host's vector path (SIMD) and the target's (scalar, E03-S01) disagree.
//
//   replay_aspmain AUD000.BIN [AUD001.BIN ...]
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "librecomp/rsp.hpp"

uint8_t dmem[0x1000];
RspExitReason dkrAspMain(uint8_t* rdram, uint32_t ucode_addr);

namespace {

constexpr uint32_t kRdramBytes = 0x800000u;

struct Capture {
    uint32_t ucode_address = 0;
    uint32_t bytes = 0;
    std::vector<uint8_t> dmem_before, rdram_before, dmem_after, rdram_after;
};

bool read_capture(const char* path, Capture& c) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) { std::perror(path); return false; }
    uint32_t header[4];
    bool ok = std::fread(header, sizeof(header), 1, f) == 1 && header[0] == 0x41524B44u &&
              header[1] == 1u;
    if (ok) {
        c.ucode_address = header[2];
        c.bytes = header[3];
        c.dmem_before.resize(0x1000);
        c.rdram_before.resize(c.bytes);
        c.dmem_after.resize(0x1000);
        c.rdram_after.resize(c.bytes);
        ok = std::fread(c.dmem_before.data(), 1, 0x1000, f) == 0x1000 &&
             std::fread(c.rdram_before.data(), 1, c.bytes, f) == c.bytes &&
             std::fread(c.dmem_after.data(), 1, 0x1000, f) == 0x1000 &&
             std::fread(c.rdram_after.data(), 1, c.bytes, f) == c.bytes;
    }
    std::fclose(f);
    if (!ok) { std::fprintf(stderr, "%s: not a version-1 audio capture\n", path); }
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<uint8_t> rdram(kRdramBytes);
    int failures = 0;
    for (int i = 1; i < argc; i++) {
        Capture c;
        if (!read_capture(argv[i], c)) { failures++; continue; }
        std::fill(rdram.begin(), rdram.end(), 0);
        std::memcpy(rdram.data(), c.rdram_before.data(), c.bytes);
        std::memcpy(dmem, c.dmem_before.data(), 0x1000);
        const RspExitReason r = dkrAspMain(rdram.data(), c.ucode_address);
        // The target ran the task on the SP thread while the guest went on
        // writing RDRAM, so the "after" image carries writes that are not the
        // task's. The host run is deterministic and alone: the bytes it changed
        // are the task's output, and that is the set compared.
        unsigned long host_wrote = 0, mismatched = 0, target_only = 0;
        uint32_t first = 0, last = 0;
        for (uint32_t a = 0; a < c.bytes; a++) {
            const bool host_changed = rdram[a] != c.rdram_before[a];
            const bool target_changed = c.rdram_after[a] != c.rdram_before[a];
            if (host_changed) {
                host_wrote++;
                if (rdram[a] != c.rdram_after[a]) {
                    if (mismatched == 0) { first = a; }
                    last = a;
                    mismatched++;
                }
            } else if (target_changed) {
                target_only++;
            }
        }
        if (std::getenv("REPLAY_DUMP") != nullptr) {
            // The host's RDRAM after the task, for offline comparison.
            const std::string out = std::string(argv[i]) + ".host";
            if (std::FILE* d = std::fopen(out.c_str(), "wb")) {
                std::fwrite(rdram.data(), 1, c.bytes, d);
                std::fclose(d);
            }
        }
        std::printf("%s: exit=%d task-wrote=%lu mismatched=%lu other-writers=%lu",
                    argv[i], static_cast<int>(r), host_wrote, mismatched, target_only);
        if (mismatched != 0) { std::printf(" [0x%06X..0x%06X]", first, last); failures++; }
        std::printf("\n");
    }
    return failures == 0 ? 0 : 1;
}
