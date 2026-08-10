#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

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

// Camera-owned matrices are rebuilt in DKR's authored frame before any level
// geometry is emitted. Give each viewport and matrix role an immutable key so
// static terrain can interpolate with camera motion without inheriting an
// object, billboard, or shadow identity. Scene generation prevents history
// from crossing a load or cutscene boundary.
constexpr std::uint32_t make_camera_matrix_identity(
    std::uint32_t scene_generation, std::uint32_t camera_id,
    std::uint8_t matrix_role, std::uint32_t continuity_epoch = 0U) {
    std::uint32_t value = 0x43414D52U;
    value ^= scene_generation * 0x9E3779B9U;
    value ^= (camera_id + 1U) * 0x85EBCA6BU;
    value ^= (static_cast<std::uint32_t>(matrix_role) + 1U) * 0xC2B2AE35U;
    value ^= mix_identity(continuity_epoch + 0x165667B1U);
    return normalise_identity(value);
}

// Every combined MVP produced under one logical viewport camera must stop
// matching history when that camera performs an authored cut. Keeping this
// key separate from the matrix role lets object, attachment, wave and scoped
// presentation identities inherit exactly the same continuity boundary.
constexpr std::uint32_t make_camera_continuity_identity(
    std::uint32_t scene_generation, std::uint32_t camera_id,
    std::uint32_t continuity_epoch) {
    std::uint32_t value = 0x43435458U;
    value ^= scene_generation * 0x9E3779B9U;
    value ^= (camera_id + 1U) * 0x85EBCA6BU;
    value ^= mix_identity(continuity_epoch + 0x165667B1U);
    return normalise_identity(value);
}

constexpr std::uint32_t with_camera_continuity(
    std::uint32_t identity, std::uint32_t camera_identity) {
    if (identity == kIgnoredIdentity || camera_identity == kIgnoredIdentity) {
        return kIgnoredIdentity;
    }
    return normalise_identity(
        identity ^ mix_identity(camera_identity + 0xD3A2646CU));
}

struct CameraContinuitySample {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float fov = 60.0F;
    std::int16_t yaw = 0;
    std::int16_t pitch = 0;
    std::int16_t roll = 0;
};

constexpr std::uint32_t camera_angle_distance(std::int16_t previous,
                                               std::int16_t current) {
    const std::uint32_t forward =
        static_cast<std::uint16_t>(current) -
        static_cast<std::uint16_t>(previous);
    const std::uint32_t wrapped = forward & 0xFFFFU;
    return std::min(wrapped, 0x10000U - wrapped);
}

// Interpolation is valid only while one authored camera follows a continuous
// path. DKR reuses its four cutscene-camera slots between shots, so the slot
// number alone cannot distinguish a smooth move from a teleport. Reject a
// pair whose position, orientation, or FOV changes far beyond one 30 Hz tick.
inline bool camera_sample_discontinuous(
    const CameraContinuitySample& previous,
    const CameraContinuitySample& current,
    float maximum_position_step = 640.0F,
    std::uint32_t maximum_angle_step = 0x3000U,
    float maximum_fov_step = 20.0F) {
    if (!std::isfinite(previous.x) || !std::isfinite(previous.y) ||
        !std::isfinite(previous.z) || !std::isfinite(previous.fov) ||
        !std::isfinite(current.x) || !std::isfinite(current.y) ||
        !std::isfinite(current.z) || !std::isfinite(current.fov) ||
        !std::isfinite(maximum_position_step) ||
        !std::isfinite(maximum_fov_step) ||
        maximum_position_step <= 0.0F || maximum_fov_step <= 0.0F) {
        return true;
    }

    const float dx = current.x - previous.x;
    const float dy = current.y - previous.y;
    const float dz = current.z - previous.z;
    const float distance_squared = dx * dx + dy * dy + dz * dz;
    const float maximum_distance_squared =
        maximum_position_step * maximum_position_step;
    return !std::isfinite(distance_squared) ||
        distance_squared > maximum_distance_squared ||
        std::abs(current.fov - previous.fov) > maximum_fov_step ||
        camera_angle_distance(previous.yaw, current.yaw) > maximum_angle_step ||
        camera_angle_distance(previous.pitch, current.pitch) > maximum_angle_step ||
        camera_angle_distance(previous.roll, current.roll) > maximum_angle_step;
}

// HQ waves alternate between authored vertex/triangle buffers every frame.
// Their matrix allocation and source-buffer addresses are therefore not
// stable identities. The WaveBlockModel entry, tile transform and viewport
// are stable across both buffers and uniquely identify the procedural surface
// draw within a scene.
constexpr std::uint32_t make_wave_matrix_identity(
    std::uint32_t scene_generation, std::uint32_t viewport_id,
    std::uint32_t wave_block_address,
    std::uint32_t transform_x_bits, std::uint32_t transform_y_bits,
    std::uint32_t transform_z_bits, std::uint32_t topology_variant) {
    std::uint32_t value = 0x57415645U;
    value ^= scene_generation * 0x9E3779B9U;
    value ^= (viewport_id + 1U) * 0x85EBCA6BU;
    value ^= mix_identity(wave_block_address + 0x7FEB352DU);
    value ^= mix_identity(transform_x_bits);
    value ^= mix_identity(transform_y_bits + 0x27D4EB2DU);
    value ^= mix_identity(transform_z_bits + 0x165667B1U);
    value ^= topology_variant * 0xC2B2AE35U;
    return normalise_identity(value);
}

// waves_render selects one of twenty-five separately packed full grids for
// each visible tile when the authored wave view distance is 5 (or nine grids
// when it is 3), plus a four-vertex fallback for empty high-density subcells.
// Full-grid selections 1..25 have the same local vertex ordering and topology;
// they merely select the authored sample region used by the stable wave block.
// Treating each selection as a new topology creates a discontinuity precisely
// when the high-detail window crosses a grid boundary. Selection 0 remains a
// distinct topology because it really is the four-vertex fallback quad.
constexpr bool valid_wave_selection_pattern(std::uint8_t selection_pattern) {
    return selection_pattern <= 25U;
}

constexpr std::uint32_t make_wave_topology_variant(
    std::uint32_t subdivisions, bool double_density,
    std::uint8_t selection_pattern, std::uint32_t scale_bits) {
    const bool fallback_quad = double_density && selection_pattern == 0U;
    const bool full_grid = selection_pattern != 0U;
    std::uint32_t value = subdivisions & 0xFFU;
    value |= static_cast<std::uint32_t>(double_density) << 8U;
    value |= static_cast<std::uint32_t>(fallback_quad) << 9U;
    value |= static_cast<std::uint32_t>(full_grid) << 10U;
    value ^= mix_identity(scale_bits) & 0xFFFFC000U;
    return value;
}

constexpr std::uint32_t make_vehicle_part_matrix_identity(
    std::uint32_t object_identity, std::uint32_t transform_address) {
    return object_identity == kIgnoredIdentity
        ? kIgnoredIdentity
        : normalise_identity(object_identity ^ 0x56504D58U ^
              ((transform_address & 0x007FFFFFU) * 0x9E3779B9U));
}

constexpr std::uint32_t make_shadow_group_identity(
    std::uint16_t token, std::uint8_t topology_epoch = 0U) {
    return token == 0U ? kIgnoredIdentity :
        normalise_identity(0x53484457U ^ static_cast<std::uint32_t>(token) ^
            (static_cast<std::uint32_t>(topology_epoch & 0x1FU) *
             0x9E3779B9U));
}

constexpr std::uint32_t make_vehicle_part_group_identity(std::uint16_t token) {
    return token == 0U ? kIgnoredIdentity :
        normalise_identity(0x56505254U ^ static_cast<std::uint32_t>(token));
}

inline constexpr std::uint8_t kInvalidVehiclePartSlot = 0xFFU;

// Vehicle attachments allocate one private 64-byte matrix inside their
// owning render_object call. The ordinal is stable across the two authored
// frame buffers, whereas the matrix address itself and the shared transform
// scratch address are not valid attachment identities.
constexpr std::uint8_t vehicle_part_attachment_slot(
    std::uint32_t first_matrix, std::uint32_t attachment_matrix) {
    constexpr std::uint32_t kMatrixBytes = 64U;
    constexpr std::uint32_t kEncodableSlots = 32U;
    if (attachment_matrix < first_matrix) {
        return kInvalidVehiclePartSlot;
    }
    const std::uint32_t offset = attachment_matrix - first_matrix;
    if ((offset % kMatrixBytes) != 0U) {
        return kInvalidVehiclePartSlot;
    }
    const std::uint32_t ordinal = offset / kMatrixBytes;
    return ordinal < kEncodableSlots
        ? static_cast<std::uint8_t>(ordinal)
        : kInvalidVehiclePartSlot;
}

constexpr std::uint8_t vehicle_part_frame_variant(
    std::uint32_t normalised_frame, std::uint16_t frame_count) {
    if (frame_count == 0U) {
        return 0U;
    }
    const std::uint32_t selected =
        ((normalised_frame & 0xFFU) * frame_count) >> 8U;
    return static_cast<std::uint8_t>(selected & 0x1FU);
}

constexpr std::uint8_t presentation_variant_for_address(
    std::uint32_t address) {
    return static_cast<std::uint8_t>(
        mix_identity(address & 0x007FFFFFU) & 0x1FU);
}

constexpr std::uint32_t make_billboard_group_identity(
    std::uint16_t token, std::uint8_t sprite_variant = 0U) {
    return token == 0U ? kIgnoredIdentity :
        normalise_identity(0x42494C4CU ^ static_cast<std::uint32_t>(token) ^
            (static_cast<std::uint32_t>(sprite_variant & 0x1FU) *
             0x9E3779B9U));
}

constexpr std::uint32_t make_surface_group_identity(
    std::uint16_t token, std::uint8_t variant) {
    return token == 0U ? kIgnoredIdentity :
        normalise_identity(0x53555246U ^ static_cast<std::uint32_t>(token) ^
            (static_cast<std::uint32_t>(variant & 0x1FU) * 0x9E3779B9U));
}

struct PresentationKey {
    std::uint16_t token = 0U;
    std::uint8_t variant = 0U;
};

struct ShadowVertexSample {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

// DKR rebuilds projected shadows every authored frame. Occasionally a terrain
// boundary keeps the same triangle/index topology while assigning a different
// world-space point to one of those vertex slots. Interpolating such unrelated
// slots creates the characteristic one-frame shadow explosion. Compare each
// batch after removing only the object's X/Z translation; Y intentionally
// remains in world space because a vehicle can jump while its shadow stays on
// the ground.
inline bool shadow_geometry_corresponds(
    std::span<const ShadowVertexSample> previous,
    std::span<const ShadowVertexSample> current,
    std::span<const std::uint16_t> batch_vertex_counts,
    float object_delta_x, float object_delta_z,
    float maximum_residual) {
    if (previous.size() != current.size() || previous.empty() ||
        !std::isfinite(object_delta_x) || !std::isfinite(object_delta_z) ||
        !std::isfinite(maximum_residual) || maximum_residual <= 0.0F) {
        return false;
    }

    const float maximum_residual_squared =
        maximum_residual * maximum_residual;
    std::size_t batch_start = 0U;
    for (const std::uint16_t count_value : batch_vertex_counts) {
        const std::size_t count = count_value;
        if (count == 0U || batch_start + count > current.size()) {
            return false;
        }

        for (std::size_t local = 0U; local < count; ++local) {
            const std::size_t index = batch_start + local;
            const ShadowVertexSample aligned{
                current[index].x - object_delta_x,
                current[index].y,
                current[index].z - object_delta_z,
            };
            const auto distance_squared = [&aligned](
                const ShadowVertexSample& candidate) {
                const float dx = aligned.x - candidate.x;
                const float dy = aligned.y - candidate.y;
                const float dz = aligned.z - candidate.z;
                return dx * dx + dy * dy + dz * dz;
            };

            const float ordered_distance = distance_squared(previous[index]);
            if (!std::isfinite(ordered_distance) ||
                ordered_distance > maximum_residual_squared) {
                return false;
            }

            float nearest_distance = ordered_distance;
            std::size_t nearest_local = local;
            for (std::size_t candidate = 0U; candidate < count; ++candidate) {
                const float candidate_distance = distance_squared(
                    previous[batch_start + candidate]);
                if (candidate_distance < nearest_distance) {
                    nearest_distance = candidate_distance;
                    nearest_local = candidate;
                }
            }

            // A substantially closer point in another slot proves that the
            // authored vertex ordering changed even if the triangle indices
            // and batch sizes did not. Snap this transition instead of
            // interpolating the wrong corners through one another.
            if (nearest_local != local &&
                ordered_distance > nearest_distance * 4.0F + 4.0F) {
                return false;
            }
        }
        batch_start += count;
    }
    return batch_start == current.size();
}

struct MatrixInterpolation {
    std::uint32_t identity = kIgnoredIdentity;
    bool interpolate_vertices = false;
    bool interpolate_texcoords = false;
    bool interpolate_tiles = false;
};

MatrixInterpolation matrix_interpolation(
    std::uint32_t physical_matrix_address);
std::uint32_t matrix_identity(std::uint32_t physical_matrix_address);
PresentationKey surface_presentation_key(std::uint32_t batch_address);
std::uint16_t presentation_token_for_object(std::uint8_t* rdram,
                                            std::uint32_t object_address);
std::uint16_t presentation_token_for_registered_object(
    std::uint8_t* rdram, std::uint32_t object_address);
std::uint16_t presentation_token_for_active_object(
    std::uint32_t object_address);

struct VehiclePartPresentationKey {
    std::uint16_t attachment_token = 0U;
    std::uint8_t attachment_slot = kInvalidVehiclePartSlot;
};

VehiclePartPresentationKey active_vehicle_part_presentation_key(
    std::uint32_t attachment_matrix_address);

std::uint32_t register_active_vehicle_part_matrix(
    std::uint32_t attachment_transform_address,
    std::uint32_t attachment_matrix_address);

struct ShadowPresentationKey {
    std::uint16_t token = 0U;
    std::uint8_t topology_epoch = 0U;
};

ShadowPresentationKey shadow_presentation_key(
    std::uint8_t* rdram, std::uint32_t object_address,
    std::uint32_t shadow_address);

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

// True only while the renderer is decoding a submitted task whose authored
// frame is safe to pair with its predecessor. Post-race spectator cameras use
// abrupt cuts and can rebuild display-list topology between consecutive
// frames, so those tasks deliberately stay at DKR's authored cadence.
bool task_interpolation_allowed();

} // namespace dkr::runtime::presentation
