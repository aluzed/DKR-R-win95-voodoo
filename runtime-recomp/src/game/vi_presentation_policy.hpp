#pragma once

#include <cstdint>

namespace dkr::runtime::presentation {

inline constexpr std::uint32_t kCanonicalViWidth = 320U;
inline constexpr std::uint32_t kCanonicalViHeight = 240U;

constexpr std::uint32_t vi_region_start(std::uint32_t region) {
    return (region >> 16U) & 0x3FFU;
}

constexpr std::uint32_t vi_region_end(std::uint32_t region) {
    return region & 0x3FFU;
}

constexpr std::uint32_t round_positive(double value) {
    return static_cast<std::uint32_t>(value + 0.5);
}

// Mirrors RT64::VI::fbSize for the progressive vertical path. RT64 adds two
// guard rows and rounds the result to a multiple of four; DKR's otherwise
// canonical 320x240 signal can consequently be inferred as 244 rows.
constexpr std::uint32_t inferred_vi_height(std::uint32_t v_region,
                                           std::uint32_t y_scale) {
    const std::uint32_t start = vi_region_start(v_region);
    const std::uint32_t end = vi_region_end(v_region);
    y_scale &= 0xFFFU;
    if (end <= start || y_scale == 0U) {
        return 0U;
    }

    const double sampled_rows =
        (static_cast<double>(end - start) * static_cast<double>(y_scale)) /
        2048.0;
    const std::uint32_t with_guard_rows = round_positive(sampled_rows) + 2U;
    return round_positive(static_cast<double>(with_guard_rows) / 4.0) * 4U;
}

// Keep the authored start line and trim only enough of RT64's inferred guard
// region for the final present to resolve to 240 rows. This is presentation
// metadata only; the game's framebuffer, projection and display lists remain
// untouched. Signals that are not the small 240/244 DKR family pass through.
constexpr std::uint32_t canonicalise_dkr_v_region(std::uint32_t v_region,
                                                  std::uint32_t y_scale) {
    const std::uint32_t height = inferred_vi_height(v_region, y_scale);
    if (height <= kCanonicalViHeight || height > kCanonicalViHeight + 8U) {
        return v_region;
    }

    const std::uint32_t start = vi_region_start(v_region);
    const std::uint32_t end = vi_region_end(v_region);
    for (std::uint32_t trim = 1U; trim <= 16U && end > start + trim; ++trim) {
        const std::uint32_t candidate =
            (v_region & 0xFFFFFC00U) | ((end - trim) & 0x3FFU);
        if (inferred_vi_height(candidate, y_scale) == kCanonicalViHeight) {
            return candidate;
        }
    }
    return v_region;
}

} // namespace dkr::runtime::presentation
