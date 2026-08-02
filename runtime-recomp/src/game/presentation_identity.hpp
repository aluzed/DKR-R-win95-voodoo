#pragma once

#include <cstdint>

namespace dkr::runtime::presentation {

inline constexpr std::uint32_t kIgnoredIdentity = 0U;
inline constexpr std::uint32_t kAutomaticIdentity = 0xFFFFFFFFU;

constexpr std::uint32_t mix_identity(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

constexpr std::uint32_t normalise_identity(std::uint32_t value) {
    value = mix_identity(value);
    if (value == kIgnoredIdentity || value == kAutomaticIdentity) {
        value ^= 0xA511E9B3U;
    }
    return value;
}

constexpr std::uint32_t make_object_identity(std::uint32_t scene_generation,
                                              std::uint32_t object_address,
                                              std::uint32_t lifetime_generation,
                                              std::uint16_t object_id,
                                              std::uint16_t behaviour_id) {
    std::uint32_t value = scene_generation * 0x9E3779B9U;
    value ^= object_address * 0x85EBCA6BU;
    value ^= lifetime_generation * 0xC2B2AE35U;
    value ^= static_cast<std::uint32_t>(object_id) << 16U;
    value ^= behaviour_id;
    return normalise_identity(value);
}

constexpr std::uint32_t make_matrix_identity(std::uint32_t object_identity,
                                              std::uint32_t matrix_ordinal) {
    return normalise_identity(object_identity ^
        ((matrix_ordinal + 1U) * 0x27D4EB2DU));
}

std::uint32_t matrix_identity(std::uint32_t physical_matrix_address);

// Activates the immutable identity sidecar captured alongside one submitted
// graphics task. The renderer owns this scope for the entire F3DDKR decode so
// a delayed task can never observe identities from a newer frame that reused
// the same N64 display-list buffer.
class TaskIdentityScope {
public:
    explicit TaskIdentityScope(std::uint32_t display_list_address);
    ~TaskIdentityScope();

    TaskIdentityScope(const TaskIdentityScope&) = delete;
    TaskIdentityScope& operator=(const TaskIdentityScope&) = delete;
};

} // namespace dkr::runtime::presentation
