#pragma once

#include "presentation_policy.hpp"

#include <algorithm>

namespace dkr::runtime::enhancements {

inline constexpr int kMinimumGameplayFov = 40;
inline constexpr int kMaximumGameplayFov = 100;
inline constexpr int kMinimumFovOffset = -20;
inline constexpr int kMaximumFovOffset = 20;
inline constexpr int kMinimumViewDistanceMultiplier = 1;
inline constexpr int kMaximumViewDistanceMultiplier = 12;
inline constexpr int kMaximumWaveViewDistance = 5;
inline constexpr int kMinimumFrustumGuardPercent = 0;
inline constexpr int kMaximumFrustumGuardPercent = 20;
inline constexpr float kOriginalAspectRatio = 4.0F / 3.0F;
inline constexpr int kRaceTypeDefault = 0;
inline constexpr int kRaceTypeHorseshoeGulch = 3;
inline constexpr int kRaceTypeHubWorld = 5;
inline constexpr int kRaceTypeBoss = 8;
inline constexpr int kRaceTypeChallengeMask = 0x40;

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

// These are the decompiled race types that own a playable world. Frontend,
// character-select and cutscene types are deliberately excluded so Modern
// persistence cannot expose work intended only for a scripted shot.
constexpr bool is_gameplay_scenery_race_type(int race_type) {
    return race_type >= 0 &&
        (race_type == kRaceTypeDefault ||
         race_type == kRaceTypeHorseshoeGulch ||
         race_type == kRaceTypeHubWorld ||
         race_type == kRaceTypeBoss ||
         (race_type & kRaceTypeChallengeMask) != 0);
}

// DKR allocates fixed selector and vertex pools for either a 3x3 or 5x5 HQ
// wave window. Five is the largest safe value; larger values index beyond the
// retail lookup tables. Promote playable maps only. Title-demo cinematics use
// the authored close-camera window; forcing their water to 5x5 multiplies the
// procedural wave workload in the Timber and Pipsy shots.
constexpr int effective_wave_view_distance(PresentationProfile profile,
                                           bool keep_scenery,
                                           int race_type,
                                           int authored_distance) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern &&
            keep_scenery && is_gameplay_scenery_race_type(race_type)
        ? kMaximumWaveViewDistance
        : authored_distance;
}

constexpr bool persistent_water_override_enabled(PresentationProfile profile,
                                                  bool keep_scenery,
                                                  int race_type) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern &&
        keep_scenery && is_gameplay_scenery_race_type(race_type);
}

constexpr int persistent_water_hq_fade(PresentationProfile profile,
                                       bool keep_scenery,
                                       bool selected_for_hq,
                                       int authored_fade) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern &&
            keep_scenery && selected_for_hq
        ? 0
        : authored_fade;
}

constexpr bool is_adventure_hub_level(int level_id) {
    // Central Area, the four themed world lobbies, and Future Fun Land.
    return level_id == 0 || level_id == 2 || level_id == 12 ||
        level_id == 14 || level_id == 24 || level_id == 35;
}

// At multipliers above retail, hub geometry is still culled normally against
// the camera but is no longer rejected solely because the camera crossed an
// authored regional visibility bitfield. This is what turns the existing
// object-distance control into a coherent scenery-distance control in hubs.
constexpr bool relax_scenery_segment_bitfield(PresentationProfile profile,
                                               int level_id, int race_type,
                                               int multiplier,
                                               bool keep_scenery) {
    if (normalise_presentation_profile(profile) != PresentationProfile::Modern) {
        return false;
    }
    if (keep_scenery && is_gameplay_scenery_race_type(race_type)) {
        return true;
    }
    return is_adventure_hub_level(level_id) &&
        clamp_view_distance_multiplier(multiplier) > 1;
}

constexpr bool force_scenery_segment_visible(PresentationProfile profile,
                                              int race_type,
                                              bool keep_scenery) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern &&
        keep_scenery && is_gameplay_scenery_race_type(race_type);
}

constexpr float hub_segment_frustum_scale(PresentationProfile profile,
                                           int level_id, int multiplier) {
    if (normalise_presentation_profile(profile) != PresentationProfile::Modern ||
        !is_adventure_hub_level(level_id)) {
        return 1.0F;
    }
    return 1.0F + 0.08F * static_cast<float>(
        clamp_view_distance_multiplier(multiplier) - 1);
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
