#pragma once

#include "presentation_policy.hpp"

namespace dkr::runtime::enhancements {

bool maximum_detail_enabled();
bool maximum_detail_requested();
void set_maximum_detail_enabled(bool enabled);

PresentationProfile presentation_profile();
void set_presentation_profile(PresentationProfile profile);
bool modern_presentation_enabled();

} // namespace dkr::runtime::enhancements
