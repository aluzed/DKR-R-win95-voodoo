#include "audio_mix_policy.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace dkr::runtime::audio;
    static_assert(clamp_mix_volume(-1.0F) == 0.0F);
    static_assert(clamp_mix_volume(2.0F) == 1.0F);
    assert(scale_authored_volume(0x12345678U, 1.0F) == 0x12345678U);
    assert(scale_authored_volume(127U, 0.0F) == 0U);
    assert(scale_authored_volume(100U, 0.5F) == 50U);
    assert(scale_authored_volume(255U, 0.5F) == 128U);
    std::puts("[test][audio-mix-policy] PASS");
    return 0;
}
