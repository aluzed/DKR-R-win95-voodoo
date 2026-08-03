#include "modern_camera_policy.hpp"

#include <cmath>
#include <cstdio>

using namespace dkr::runtime::enhancements;

static_assert(clamp_fov_offset(-99) == -20);
static_assert(clamp_fov_offset(99) == 40);
static_assert(effective_gameplay_fov(PresentationProfile::Accurate, 60, 40) == 60);
static_assert(effective_gameplay_fov(PresentationProfile::Modern, 60, 10) == 70);
static_assert(effective_gameplay_fov(PresentationProfile::Modern, 80, 40) == 100);
static_assert(effective_gameplay_fov(PresentationProfile::Modern, 45, -20) == 40);
static_assert(effective_view_distance(PresentationProfile::Accurate, 1000, 6) == 1000);
static_assert(effective_view_distance(PresentationProfile::Modern, 1000, 3) == 3000);
static_assert(effective_view_distance(PresentationProfile::Modern, 10000, 6) == 32767);
static_assert(effective_view_distance(PresentationProfile::Modern, -1, 6) == -1);
static_assert(active_viewport_aspect(16.0F / 9.0F, 0) == 16.0F / 9.0F);
static_assert(active_viewport_aspect(16.0F / 9.0F, 1) == 32.0F / 9.0F);
static_assert(active_viewport_aspect(16.0F / 9.0F, 3) == 16.0F / 9.0F);
static_assert(frustum_horizontal_scale(PresentationProfile::Accurate, true, true,
                                       32.0F / 9.0F, 0, 5) == 1.0F);
static_assert(frustum_horizontal_scale(PresentationProfile::Modern, false, true,
                                       32.0F / 9.0F, 0, 5) == 1.0F);

int main() {
    const float scale_16_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 16.0F / 9.0F, 0, 5);
    const float scale_32_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 32.0F / 9.0F, 0, 5);
    const float scale_split = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 16.0F / 9.0F, 1, 5);
    if (std::abs(scale_16_9 - 1.4F) > 0.0001F ||
        std::abs(scale_32_9 - 2.8F) > 0.0001F ||
        std::abs(scale_split - 2.8F) > 0.0001F) {
        std::fputs("[test][modern-camera-policy] FAIL\n", stderr);
        return 1;
    }
    std::puts("[test][modern-camera-policy] PASS");
    return 0;
}
