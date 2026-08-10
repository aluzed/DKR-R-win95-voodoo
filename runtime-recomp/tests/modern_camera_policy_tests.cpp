#include "modern_camera_policy.hpp"

#include <cmath>
#include <cstdio>

using namespace dkr::runtime::enhancements;

static_assert(clamp_fov_offset(-99) == -20);
static_assert(clamp_fov_offset(99) == 20);
static_assert(clamp_fov_offset(40) == 20);
static_assert(effective_gameplay_fov(PresentationProfile::Accurate, 60, 40) == 60);
static_assert(effective_gameplay_fov(PresentationProfile::Modern, 60, 10) == 70);
static_assert(effective_gameplay_fov(PresentationProfile::Modern, 80, 40) == 100);
static_assert(effective_gameplay_fov(PresentationProfile::Modern, 45, -20) == 40);
static_assert(effective_view_distance(PresentationProfile::Accurate, 1000, 6) == 1000);
static_assert(effective_view_distance(PresentationProfile::Modern, 1000, 3) == 3000);
static_assert(effective_view_distance(PresentationProfile::Modern, 10000, 6) == 32767);
static_assert(clamp_view_distance_multiplier(99) == 12);
static_assert(effective_view_distance(PresentationProfile::Modern, 1000, 12) == 12000);
static_assert(effective_view_distance(PresentationProfile::Modern, -1, 6) == -1);
static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, true, kRaceTypeDefault, 3) == 5);
static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, false, kRaceTypeDefault, 3) == 3);
static_assert(effective_wave_view_distance(
    PresentationProfile::Accurate, true, kRaceTypeDefault, 3) == 3);
static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, true, 6, 3) == 3);
static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, true, 7, 3) == 3);
static_assert(persistent_water_override_enabled(
    PresentationProfile::Modern, true, kRaceTypeDefault));
static_assert(persistent_water_override_enabled(
    PresentationProfile::Modern, true, 0x42));
static_assert(!persistent_water_override_enabled(
    PresentationProfile::Modern, true, 6));
static_assert(!persistent_water_override_enabled(
    PresentationProfile::Modern, true, 7));
static_assert(persistent_water_hq_fade(
    PresentationProfile::Modern, true, true, 0x80) == 0);
static_assert(persistent_water_hq_fade(
    PresentationProfile::Modern, true, false, 0x80) == 0x80);
static_assert(persistent_water_hq_fade(
    PresentationProfile::Modern, false, true, 0x40) == 0x40);
static_assert(persistent_water_hq_fade(
    PresentationProfile::Accurate, true, true, 0x20) == 0x20);
static_assert(is_adventure_hub_level(0));
static_assert(is_adventure_hub_level(35));
static_assert(!is_adventure_hub_level(3));
static_assert(is_gameplay_scenery_race_type(kRaceTypeDefault));
static_assert(is_gameplay_scenery_race_type(kRaceTypeHorseshoeGulch));
static_assert(is_gameplay_scenery_race_type(kRaceTypeHubWorld));
static_assert(is_gameplay_scenery_race_type(kRaceTypeBoss));
static_assert(is_gameplay_scenery_race_type(0x40));
static_assert(is_gameplay_scenery_race_type(0x42));
static_assert(!is_gameplay_scenery_race_type(1));
static_assert(!is_gameplay_scenery_race_type(6));
static_assert(!is_gameplay_scenery_race_type(7));
static_assert(!is_gameplay_scenery_race_type(-1));
static_assert(!relax_scenery_segment_bitfield(
    PresentationProfile::Accurate, 0, kRaceTypeHubWorld, 6, true));
static_assert(!relax_scenery_segment_bitfield(
    PresentationProfile::Modern, 0, kRaceTypeHubWorld, 1, false));
static_assert(relax_scenery_segment_bitfield(
    PresentationProfile::Modern, 0, kRaceTypeHubWorld, 2, false));
static_assert(relax_scenery_segment_bitfield(
    PresentationProfile::Modern, 3, kRaceTypeDefault, 1, true));
static_assert(relax_scenery_segment_bitfield(
    PresentationProfile::Modern, 11, 0x40, 1, true));
static_assert(!relax_scenery_segment_bitfield(
    PresentationProfile::Modern, 36, 6, 6, true));
static_assert(force_scenery_segment_visible(
    PresentationProfile::Modern, kRaceTypeHubWorld, true));
static_assert(force_scenery_segment_visible(
    PresentationProfile::Modern, kRaceTypeDefault, true));
static_assert(force_scenery_segment_visible(
    PresentationProfile::Modern, 0x41, true));
static_assert(!force_scenery_segment_visible(
    PresentationProfile::Modern, 6, true));
static_assert(!force_scenery_segment_visible(
    PresentationProfile::Modern, kRaceTypeHubWorld, false));
static_assert(hub_segment_frustum_scale(
    PresentationProfile::Modern, 0, 1) == 1.0F);
static_assert(hub_segment_frustum_scale(
    PresentationProfile::Modern, 0, 6) == 1.4F);
static_assert(hub_segment_frustum_scale(
    PresentationProfile::Modern, 0, 12) == 1.88F);
static_assert(hub_segment_frustum_scale(
    PresentationProfile::Modern, 3, 6) == 1.0F);
static_assert(active_viewport_aspect(16.0F / 9.0F, 0) == 16.0F / 9.0F);
static_assert(active_viewport_aspect(16.0F / 9.0F, 1) == 32.0F / 9.0F);
static_assert(active_viewport_aspect(16.0F / 9.0F, 3) == 16.0F / 9.0F);
static_assert(frustum_horizontal_scale(PresentationProfile::Accurate, true, true,
                                       32.0F / 9.0F, 0, 5) == 1.0F);
static_assert(frustum_horizontal_scale(PresentationProfile::Modern, false, true,
                                       32.0F / 9.0F, 0, 5) == 1.0F);

int main() {
    const float scale_4_3 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 4.0F / 3.0F, 0, 5);
    const float scale_16_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 16.0F / 9.0F, 0, 5);
    const float scale_21_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 21.0F / 9.0F, 0, 5);
    const float scale_32_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 32.0F / 9.0F, 0, 5);
    const float scale_split = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 16.0F / 9.0F, 1, 5);
    if (std::abs(scale_4_3 - 1.05F) > 0.0001F ||
        std::abs(scale_16_9 - 1.4F) > 0.0001F ||
        std::abs(scale_21_9 - 1.8375F) > 0.0001F ||
        std::abs(scale_32_9 - 2.8F) > 0.0001F ||
        std::abs(scale_split - 2.8F) > 0.0001F) {
        std::fputs("[test][modern-camera-policy] FAIL\n", stderr);
        return 1;
    }
    std::puts("[test][modern-camera-policy] PASS");
    return 0;
}
