#pragma once

#include "presentation_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::interpolation {

// F3DDKR exposes three independently selectable combined-MVP slots. RT64's
// interpolation identity is state beside that matrix, so selecting an older
// slot must restore both pieces together.
inline constexpr std::size_t kMatrixSlotCount = 3U;
inline constexpr std::size_t kMaxPresentationScopeDepth = 32U;
inline constexpr std::uint32_t kTitleMenuId = 0U;

// The title demo contains two water-heavy scripted shots. RT64's tile matcher
// has no stable one-to-one pairing for their rapidly recycled procedural wave
// tiles and can spend tens of milliseconds searching each authored frame.
// Geometry and UV interpolation remain enabled; only tile-state interpolation
// falls back to the authored cadence while the title demo owns the frontend.
constexpr bool effective_tile_interpolation(bool requested,
                                            std::uint32_t current_menu_id) {
    return requested && current_menu_id != kTitleMenuId;
}

struct Group {
    std::uint32_t identity = presentation::kIgnoredIdentity;
    bool interpolate_vertices = false;
    bool interpolate_texcoords = false;
    bool interpolate_tiles = false;
    std::uint8_t mode = 0U;
};

struct EndScopeResult {
    Group ended{};
    bool had_scope = false;
};

class GroupState {
public:
    constexpr void reset() {
        matrix_groups_ = {};
        scopes_ = {};
        selected_matrix_ = 0U;
        scope_depth_ = 0U;
        rejected_scope_begins_ = 0U;
    }

    constexpr void load_matrix(std::size_t slot, std::uint32_t identity,
                               bool interpolate_vertices = false,
                               bool interpolate_texcoords = false,
                               bool interpolate_tiles = false) {
        selected_matrix_ = clamp_slot(slot);
        matrix_groups_[selected_matrix_] = Group{
            identity, interpolate_vertices, interpolate_texcoords,
            interpolate_tiles, 0U};
    }

    constexpr void select_matrix(std::size_t slot) {
        selected_matrix_ = clamp_slot(slot);
    }

    [[nodiscard]] constexpr std::size_t selected_matrix() const {
        return selected_matrix_;
    }

    [[nodiscard]] constexpr Group matrix_group(std::size_t slot) const {
        return matrix_groups_[clamp_slot(slot)];
    }

    [[nodiscard]] constexpr bool begin_scope(std::uint8_t mode,
                                             std::uint32_t identity,
                                             bool interpolate_vertices,
                                             bool interpolate_texcoords = false,
                                             bool interpolate_tiles = false) {
        if (scope_depth_ >= scopes_.size()) {
            ++rejected_scope_begins_;
            return false;
        }
        scopes_[scope_depth_++] = Group{
            identity, interpolate_vertices, interpolate_texcoords,
            interpolate_tiles, mode};
        return true;
    }

    [[nodiscard]] constexpr EndScopeResult end_scope() {
        if (scope_depth_ == 0U) {
            return {};
        }
        const Group ended = scopes_[--scope_depth_];
        scopes_[scope_depth_] = {};
        return {ended, true};
    }

    [[nodiscard]] constexpr Group active_group() const {
        return scope_depth_ != 0U
            ? scopes_[scope_depth_ - 1U]
            : matrix_groups_[selected_matrix_];
    }

    [[nodiscard]] constexpr bool has_active_scope() const {
        return scope_depth_ != 0U;
    }

    [[nodiscard]] constexpr std::size_t scope_depth() const {
        return scope_depth_;
    }

    [[nodiscard]] constexpr std::uint32_t rejected_scope_begins() const {
        return rejected_scope_begins_;
    }

    [[nodiscard]] constexpr bool contains_mode(std::uint8_t mode) const {
        for (std::size_t index = 0U; index < scope_depth_; ++index) {
            if (scopes_[index].mode == mode) {
                return true;
            }
        }
        return false;
    }

private:
    [[nodiscard]] static constexpr std::size_t clamp_slot(std::size_t slot) {
        return slot < kMatrixSlotCount ? slot : kMatrixSlotCount - 1U;
    }

    std::array<Group, kMatrixSlotCount> matrix_groups_{};
    std::array<Group, kMaxPresentationScopeDepth> scopes_{};
    std::size_t selected_matrix_ = 0U;
    std::size_t scope_depth_ = 0U;
    std::uint32_t rejected_scope_begins_ = 0U;
};

} // namespace dkr::runtime::interpolation
