#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace dkr::runtime::audio {

constexpr float clamp_mix_volume(float volume) {
    return std::clamp(volume, 0.0F, 1.0F);
}

inline std::uint32_t scale_authored_volume(std::uint32_t value, float volume) {
    volume = clamp_mix_volume(volume);
    if (volume >= 0.9999F) {
        return value;
    }
    return static_cast<std::uint32_t>(std::clamp(
        std::llround(static_cast<double>(value) * static_cast<double>(volume)),
        0LL, static_cast<long long>(std::numeric_limits<std::uint32_t>::max())));
}

} // namespace dkr::runtime::audio
