#pragma once

#include <algorithm>

namespace dkr::runtime::enhancements {

inline constexpr int kCharacterSelectFallbackTempo = 182;

constexpr int character_select_tempo(int tempo) {
    return tempo > 0 && tempo <= 400 ? tempo : kCharacterSelectFallbackTempo;
}

constexpr float advance_character_select_phase(float phase, int update_rate,
                                               int tempo) {
    const int bounded_update_rate = std::clamp(update_rate, 0, 4);
    float next = phase + static_cast<float>(bounded_update_rate) *
        static_cast<float>(character_select_tempo(tempo)) / 3600.0F;
    while (next >= 1.0F) {
        next -= 1.0F;
    }
    while (next < 0.0F) {
        next += 1.0F;
    }
    return next;
}

} // namespace dkr::runtime::enhancements
