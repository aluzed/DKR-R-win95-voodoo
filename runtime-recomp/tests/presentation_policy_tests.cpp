#include "presentation_policy.hpp"

#include <cstdio>

using dkr::runtime::enhancements::PresentationProfile;
using dkr::runtime::enhancements::clamp_presentation_rate;
using dkr::runtime::enhancements::interpolation_allowed;
using dkr::runtime::enhancements::maximum_detail_effective;
using dkr::runtime::enhancements::normalise_presentation_profile;
using dkr::runtime::enhancements::resolve_settings_profile;

static_assert(normalise_presentation_profile(-1) == PresentationProfile::Accurate);
static_assert(normalise_presentation_profile(0) == PresentationProfile::Accurate);
static_assert(normalise_presentation_profile(1) == PresentationProfile::Modern);
static_assert(normalise_presentation_profile(2) == PresentationProfile::Accurate);
static_assert(clamp_presentation_rate(1) == 30);
static_assert(clamp_presentation_rate(120) == 120);
static_assert(clamp_presentation_rate(1000) == 500);
static_assert(!maximum_detail_effective(PresentationProfile::Accurate, true));
static_assert(maximum_detail_effective(PresentationProfile::Modern, true));
static_assert(!maximum_detail_effective(PresentationProfile::Modern, false));
static_assert(!interpolation_allowed(PresentationProfile::Accurate));
static_assert(interpolation_allowed(PresentationProfile::Modern));
static_assert(resolve_settings_profile(0, false, PresentationProfile::Modern) ==
              PresentationProfile::Accurate);
static_assert(resolve_settings_profile(3, true, PresentationProfile::Modern) ==
              PresentationProfile::Accurate);
static_assert(resolve_settings_profile(4, false, PresentationProfile::Modern) ==
              PresentationProfile::Accurate);
static_assert(resolve_settings_profile(4, true, PresentationProfile::Modern) ==
              PresentationProfile::Modern);
static_assert(resolve_settings_profile(
                  4, true, static_cast<PresentationProfile>(99)) ==
              PresentationProfile::Accurate);

int main() {
    std::puts("[test][presentation-policy] PASS");
    return 0;
}
