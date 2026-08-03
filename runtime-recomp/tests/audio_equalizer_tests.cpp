#include "audio_equalizer.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    dkr::runtime::audio::StereoEqualizer equalizer;
    equalizer.configure(32000, 0.0F, 0.0F, 0.0F);
    assert(equalizer.neutral());
    const auto neutral = equalizer.process(1234.0F, -4321.0F);
    assert(neutral.first == 1234.0F && neutral.second == -4321.0F);

    equalizer.configure(32000, 6.0F, -3.0F, 4.0F);
    assert(!equalizer.neutral());
    for (int index = 0; index < 10000; ++index) {
        const float sample = std::sin(static_cast<float>(index) * 0.1F) * 12000.0F;
        const auto filtered = equalizer.process(sample, -sample);
        assert(std::isfinite(filtered.first));
        assert(std::isfinite(filtered.second));
    }
    std::puts("[test][audio-equalizer] PASS");
    return 0;
}
