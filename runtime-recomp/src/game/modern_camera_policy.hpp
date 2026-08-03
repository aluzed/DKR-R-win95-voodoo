#pragma once

#include "presentation_policy.hpp"

#include <algorithm>

namespace dkr::runtime::enhancements {

inline constexpr int kMinimumGameplayFov = 40;
inline constexpr int kMaximumGameplayFov = 100;
inline constexpr int kMinimumFovOffset = -20;
inline constexpr int kMaximumFovOffset = 40;
inline constexpr int kMinimumViewDistanceMultiplier = 1;
inline constexpr int kMaximumViewDistanceMultiplier = 6;
inline constexpr int kMinimumFrustumGuardPercent = 0;
inline constexpr int kMaximumFrustumGuardPercent = 20;
inline constexpr float kOriginalAspectRatio = 4.0F / 3.0F;

constexpr int clamp_fov_offset(int offset) {
    return std::clamp(offset, kMinimumFovOffset, kMaximumFovOffset);
}

constexpr int effective_gameplay_fov(PresentationProfile profile,
                                     int authored_fov, int offset) {
    if (normalise_presentation_profile(profile) != PresentationProfile::Modern) {
        return authored_fov;
    }
    return std::clamp(authored_fov + clamp_fov_offset(offset),
                      kMinimumGameplayFov, kMaximumGameplayFov);
}

constexpr int clamp_view_distance_multiplier(int multiplier) {
    return std::clamp(multiplier, kMinimumViewDistanceMultiplier,
                      kMaximumViewDistanceMultiplier);
}

constexpr int effective_view_distance(PresentationProfile profile,
                                      int authored_distance, int multiplier) {
    if (normalise_presentation_profile(profile) != PresentationProfile::Modern ||
        authored_distance <= 0) {
        return authored_distance;
    }
    const int scaled = authored_distance *
        clamp_view_distance_multiplier(multiplier);
    return std::min(scaled, 32767);
}

constexpr int clamp_frustum_guard_percent(int percent) {
    return std::clamp(percent, kMinimumFrustumGuardPercent,
                      kMaximumFrustumGuardPercent);
}

// DKR's two-player mode is split horizontally, so each player's viewport is
// twice as wide relative to its height. Three/four-player layouts use
// quadrants and therefore retain the window's aspect ratio.
constexpr float active_viewport_aspect(float window_aspect,
                                       int viewport_layout) {
    return viewport_layout == 1 ? window_aspect * 2.0F : window_aspect;
}

constexpr float frustum_horizontal_scale(PresentationProfile profile,
                                         bool fit_to_window,
                                         bool extended_culling,
                                         float window_aspect,
                                         int viewport_layout,
                                         int guard_percent) {
    if (normalise_presentation_profile(profile) != PresentationProfile::Modern ||
        !fit_to_window || !extended_culling || window_aspect <= 0.0F) {
        return 1.0F;
    }
    const float aspect_scale = std::max(
        1.0F,
        active_viewport_aspect(window_aspect, viewport_layout) /
            kOriginalAspectRatio);
    const float guard = 1.0F +
        static_cast<float>(clamp_frustum_guard_percent(guard_percent)) /
            100.0F;
    return aspect_scale * guard;
}

} // namespace dkr::runtime::enhancements
