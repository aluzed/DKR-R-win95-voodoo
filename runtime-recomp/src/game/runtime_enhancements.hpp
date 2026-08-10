#pragma once

#include "presentation_policy.hpp"

namespace dkr::runtime::enhancements {

bool maximum_detail_enabled();
bool maximum_detail_requested();
void set_maximum_detail_enabled(bool enabled);

PresentationProfile presentation_profile();
void set_presentation_profile(PresentationProfile profile);
bool modern_presentation_enabled();

int fov_offset();
void set_fov_offset(int offset);
int view_distance_multiplier();
void set_view_distance_multiplier(int multiplier);
bool keep_hub_scenery_requested();
bool keep_hub_scenery_enabled();
void set_keep_hub_scenery_enabled(bool enabled);
bool extended_culling_requested();
bool extended_culling_enabled();
void set_extended_culling_enabled(bool enabled);
int frustum_guard_percent();
void set_frustum_guard_percent(int percent);
bool fit_to_window_enabled();
void set_fit_to_window_enabled(bool enabled);
int anisotropy_level();
void set_anisotropy_level(int level);

} // namespace dkr::runtime::enhancements
