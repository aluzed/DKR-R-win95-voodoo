#include "hud_placement_policy.hpp"

#include <cstdio>

using dkr::runtime::enhancements::HudAnchor;
using dkr::runtime::enhancements::classify_hud_anchor;

static_assert(classify_hud_anchor(0, 0, true, 53.0F) == HudAnchor::Left);
static_assert(classify_hud_anchor(10, 13, true, 239.0F) == HudAnchor::Right);
static_assert(classify_hud_anchor(-1, 9, true, 160.0F) == HudAnchor::None);
static_assert(classify_hud_anchor(2, 1, false, -120.0F) == HudAnchor::Left);
static_assert(classify_hud_anchor(15, 14, false, 35.0F) == HudAnchor::Right);
static_assert(classify_hud_anchor(38, 46, false, 122.0F) == HudAnchor::Right);
static_assert(classify_hud_anchor(12, 7, false, -120.0F) == HudAnchor::None);
static_assert(classify_hud_anchor(14, 5, false, -200.0F) == HudAnchor::None);
static_assert(classify_hud_anchor(35, 35, false, -120.0F) == HudAnchor::None);

int main() {
    std::puts("[test][hud-placement-policy] PASS");
    return 0;
}
