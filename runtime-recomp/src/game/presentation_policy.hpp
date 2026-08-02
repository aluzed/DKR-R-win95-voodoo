#pragma once

#include <cstdint>

namespace dkr::runtime::enhancements {

enum class PresentationProfile : std::uint8_t {
    Accurate = 0,
    Modern = 1,
};

inline constexpr int kMinimumPresentationRate = 30;
inline constexpr int kMaximumPresentationRate = 500;

constexpr PresentationProfile normalise_presentation_profile(int value) {
    return value == static_cast<int>(PresentationProfile::Modern)
        ? PresentationProfile::Modern
        : PresentationProfile::Accurate;
}

constexpr PresentationProfile normalise_presentation_profile(
    PresentationProfile profile) {
    return normalise_presentation_profile(static_cast<int>(profile));
}

constexpr int clamp_presentation_rate(int rate) {
    return rate < kMinimumPresentationRate
        ? kMinimumPresentationRate
        : rate > kMaximumPresentationRate
            ? kMaximumPresentationRate
            : rate;
}

constexpr bool maximum_detail_effective(PresentationProfile profile,
                                         bool requested) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern &&
           requested;
}

constexpr bool interpolation_allowed(PresentationProfile profile) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern;
}

constexpr PresentationProfile resolve_settings_profile(
    int settings_version, bool settings_complete,
    PresentationProfile requested_profile) {
    return settings_version == 4 && settings_complete
        ? normalise_presentation_profile(requested_profile)
        : PresentationProfile::Accurate;
}

} // namespace dkr::runtime::enhancements
