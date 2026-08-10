#include "vi_presentation_policy.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace dkr::runtime::presentation;

    // A common 480 half-line DKR signal is rounded by RT64 to 244 rows.
    constexpr std::uint32_t padded_region = (32U << 16U) | 512U;
    static_assert(inferred_vi_height(padded_region, 0x400U) == 244U);
    constexpr std::uint32_t canonical_region =
        canonicalise_dkr_v_region(padded_region, 0x400U);
    static_assert(vi_region_start(canonical_region) == 32U);
    static_assert(vi_region_end(canonical_region) == 510U);
    static_assert(inferred_vi_height(canonical_region, 0x400U) == 240U);

    // An already canonical NTSC signal is bit-exact.
    constexpr std::uint32_t retail_region = (37U << 16U) | 511U;
    static_assert(inferred_vi_height(retail_region, 0x400U) == 240U);
    static_assert(canonicalise_dkr_v_region(retail_region, 0x400U) ==
                  retail_region);

    // Blank, malformed and unrelated modes are never rewritten.
    static_assert(canonicalise_dkr_v_region(0U, 0U) == 0U);
    constexpr std::uint32_t tall_region = (20U << 16U) | 540U;
    static_assert(inferred_vi_height(tall_region, 0x400U) > 248U);
    static_assert(canonicalise_dkr_v_region(tall_region, 0x400U) == tall_region);

    assert(inferred_vi_height(canonical_region, 0x400U) == 240U);
    std::puts("[test][vi-presentation-policy] PASS");
    return 0;
}
