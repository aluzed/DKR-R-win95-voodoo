#include "widescreen_policy.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace dkr::runtime::enhancements;

int main() {
    static_assert(sky_vertical_cover_scale(1.0F) == 1.0F);
    static_assert(sky_vertical_cover_scale(4.0F / 3.0F) == 4.0F / 3.0F);
    static_assert(expanded_postrace_left(1.0F) == 0);
    static_assert(expanded_postrace_right(1.0F) == 320);
    static_assert(expanded_postrace_left(4.0F / 3.0F) == -53);
    static_assert(expanded_postrace_right(4.0F / 3.0F) == 373);

    assert(std::fabs(sky_vertical_cover_scale(7.0F / 4.0F) -
                     7.0F / 4.0F) < 0.0001F);
    const float super_ultrawide = sky_vertical_cover_scale(8.0F / 3.0F);
    assert(super_ultrawide < kSkyVerticalCorrectionCover);
    assert(std::fabs(super_ultrawide - 1.1484375F) < 0.0001F);
    std::puts("[test][widescreen-policy] PASS");
    return 0;
}
