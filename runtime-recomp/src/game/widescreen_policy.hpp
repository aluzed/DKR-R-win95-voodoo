#pragma once

#include <algorithm>

namespace dkr::runtime::enhancements {

inline constexpr float kOriginalPresentationAspect = 4.0F / 3.0F;
inline constexpr float kSkyVerticalCorrectionAspect = 21.0F / 9.0F;
inline constexpr float kSkyVerticalCorrectionCover =
    kSkyVerticalCorrectionAspect / kOriginalPresentationAspect;

// Preserve the player-approved uniform skydome cover through 21:9. Beyond
// that point, applying the full horizontal cover to the vertical projection
// drives DKR's authored horizon progressively upward. Invert only the excess
// vertical zoom, continuously at the 21:9 boundary, while horizontal coverage
// continues to grow with the viewport.
constexpr float sky_vertical_cover_scale(float horizontal_cover) {
    const float cover = std::max(horizontal_cover, 1.0F);
    if (cover <= kSkyVerticalCorrectionCover) {
        return cover;
    }
    return (kSkyVerticalCorrectionCover * kSkyVerticalCorrectionCover) /
        cover;
}

constexpr int expanded_postrace_left(float horizontal_cover) {
    const float cover = std::max(horizontal_cover, 1.0F);
    return -static_cast<int>(160.0F * (cover - 1.0F));
}

constexpr int expanded_postrace_right(float horizontal_cover) {
    return 320 - expanded_postrace_left(horizontal_cover);
}

} // namespace dkr::runtime::enhancements
