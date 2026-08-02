#pragma once

#include <cstdint>

namespace dkr::runtime::enhancements {

enum class PresentationProfile : std::uint8_t {
    Accurate = 0,
    Modern = 1,
};

bool maximum_detail_enabled();
void set_maximum_detail_enabled(bool enabled);

PresentationProfile presentation_profile();
void set_presentation_profile(PresentationProfile profile);
bool modern_presentation_enabled();

} // namespace dkr::runtime::enhancements
