// E03-S01 - differential test of the RSP vector unit's two implementations.
//
// librecomp carries every vector instruction twice: a SIMD path (SSE4.1 or
// NEON) that every modern target runs, and a scalar "SISD" path that only a
// processor without SSE4.1 takes -- the Windows 95 target's Pentium II. A DKR
// audio task replayed on the host through both disagrees on nearly every sample
// it writes, and the target agrees with the scalar path. So the scalar path is
// the suspect, and this finds where.
//
// Build it twice, once normally and once against headers that force the scalar
// path (tools/audio/build-host-tools.sh does both), run both, and diff: each
// line is an instruction, an element selector and a hash of every result over
// the same random trials, followed by the first trial's full result.
#include <cstdint>
#include <cstdio>
#include <utility>

#include "librecomp/rsp.hpp"
#include "librecomp/rsp_vu_impl.hpp"

uint8_t dmem[0x1000];

namespace {

uint32_t rng_state = 0x12345678u;
uint32_t rng() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
uint16_t rnd16() {
    static const uint16_t edges[] = {0x0000, 0x0001, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0x4000, 0xC000};
    return (rng() & 3) == 0 ? edges[rng() & 7] : static_cast<uint16_t>(rng());
}
void randomize(RSP::r128& r) {
    for (int i = 0; i < 8; i++) { r.u16(i) = rnd16(); }
}
// The flag registers only ever hold a mask per lane, 0 or 0xFFFF. The two paths
// read them differently -- the scalar one as a bool, the SIMD one as -1 -- and
// agree only on masks, so random values would report differences that no
// microcode can reach.
void randomize_mask(RSP::r128& r) {
    for (int i = 0; i < 8; i++) { r.u16(i) = (rng() & 1) ? 0xFFFF : 0x0000; }
}

uint64_t hash_r(uint64_t h, const RSP::r128& r) {
    for (int i = 0; i < 8; i++) { h = (h ^ r.u16(i)) * 0x100000001B3ull; }
    return h;
}

uint64_t hash_state(const RSP& rsp, const RSP::r128& vd) {
    uint64_t h = 0xCBF29CE484222325ull;
    h = hash_r(h, vd);
    h = hash_r(h, rsp.vpu.acch); h = hash_r(h, rsp.vpu.accm); h = hash_r(h, rsp.vpu.accl);
    h = hash_r(h, rsp.vpu.vcoh); h = hash_r(h, rsp.vpu.vcol);
    h = hash_r(h, rsp.vpu.vcch); h = hash_r(h, rsp.vpu.vccl); h = hash_r(h, rsp.vpu.vce);
    return h;
}

void print_r(const char* name, const RSP::r128& r) {
    std::printf(" %s=", name);
    for (int i = 0; i < 8; i++) { std::printf("%04X", r.u16(i)); }
}

template <typename Op>
void run(const char* name, int e, Op op) {
    RSP rsp{};
    uint64_t total = 0xCBF29CE484222325ull;
    rng_state = 0x9E3779B9u ^ static_cast<uint32_t>(e * 7919);
    for (int trial = 0; trial < 2000; trial++) {
        randomize(rsp.vpu.r[1]); randomize(rsp.vpu.r[2]); randomize(rsp.vpu.r[3]);
        randomize(rsp.vpu.acch); randomize(rsp.vpu.accm); randomize(rsp.vpu.accl);
        randomize_mask(rsp.vpu.vcoh); randomize_mask(rsp.vpu.vcol);
        randomize_mask(rsp.vpu.vcch); randomize_mask(rsp.vpu.vccl); randomize_mask(rsp.vpu.vce);
        const RSP::r128 vs = rsp.vpu.r[2], vt = rsp.vpu.r[3];
        const RSP::r128 acch = rsp.vpu.acch, accm = rsp.vpu.accm, accl = rsp.vpu.accl;
        op(rsp);
        total = total * 31 + hash_state(rsp, rsp.vpu.r[1]);
        if (trial == 0) {
            std::printf("%-6s e=%2d first:", name, e);
            print_r("vs", vs); print_r("vt", vt);
            print_r("acch", acch); print_r("accm", accm); print_r("accl", accl);
            std::printf(" ->");
            print_r("vd", rsp.vpu.r[1]);
            print_r("acch", rsp.vpu.acch); print_r("accm", rsp.vpu.accm); print_r("accl", rsp.vpu.accl);
            print_r("vcc", rsp.vpu.vccl); print_r("vco", rsp.vpu.vcol);
            std::printf("\n");
        }
    }
    std::printf("%-6s e=%2d hash=%016llX\n", name, e, static_cast<unsigned long long>(total));
}

#define TEST3(OP)                                                                       \
    template <std::size_t... E> void test_##OP(std::index_sequence<E...>) {             \
        (run(#OP, E, [](RSP& r) { r.OP<E>(r.vpu.r[1], r.vpu.r[2], r.vpu.r[3]); }), ...); \
    }
TEST3(VMADH) TEST3(VMULF) TEST3(VXOR) TEST3(VMADN) TEST3(VMACF) TEST3(VADD)
TEST3(VMUDN) TEST3(VAND) TEST3(VMADM) TEST3(VMUDM) TEST3(VADDC) TEST3(VMUDL)
TEST3(VGE) TEST3(VCL) TEST3(VMUDH) TEST3(VSUB)

template <std::size_t... E> void test_VSAR(std::index_sequence<E...>) {
    (run("VSAR", E, [](RSP& r) { r.VSAR<E>(r.vpu.r[1], r.vpu.r[2]); }), ...);
}

}  // namespace

int main() {
    using All = std::make_index_sequence<16>;
    test_VMADH(All{}); test_VMULF(All{}); test_VXOR(All{}); test_VMADN(All{});
    test_VMACF(All{}); test_VADD(All{}); test_VMUDN(All{}); test_VAND(All{});
    test_VMADM(All{}); test_VMUDM(All{}); test_VADDC(All{}); test_VMUDL(All{});
    test_VGE(All{}); test_VCL(All{}); test_VMUDH(All{}); test_VSUB(All{});
    test_VSAR(All{});
    return 0;
}
