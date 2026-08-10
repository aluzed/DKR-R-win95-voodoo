#include "presentation_identity.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using namespace dkr::runtime::presentation;

constexpr auto kObjectA = make_object_identity(3U, 0x12340U, 7U, 12U, 5U);
constexpr auto kObjectARepeat =
    make_object_identity(3U, 0x12340U, 7U, 12U, 5U);
constexpr auto kObjectNextLife =
    make_object_identity(3U, 0x12340U, 8U, 12U, 5U);
constexpr auto kObjectNextScene =
    make_object_identity(4U, 0x12340U, 7U, 12U, 5U);

static_assert(kObjectA == kObjectARepeat);
static_assert(kObjectA != kObjectNextLife);
static_assert(kObjectA != kObjectNextScene);
static_assert(kObjectA != kIgnoredIdentity && kObjectA != kAutomaticIdentity);
static_assert(make_matrix_identity(kObjectA, 0U) !=
              make_matrix_identity(kObjectA, 1U));
static_assert(make_matrix_identity(kObjectA, 0U) != kIgnoredIdentity);
static_assert(make_matrix_identity(kObjectA, 0U) != kAutomaticIdentity);
static_assert(make_camera_matrix_identity(3U, 0U, 0U) ==
              make_camera_matrix_identity(3U, 0U, 0U));
static_assert(make_camera_matrix_identity(3U, 0U, 0U) !=
              make_camera_matrix_identity(4U, 0U, 0U));
static_assert(make_camera_matrix_identity(3U, 0U, 0U) !=
              make_camera_matrix_identity(3U, 1U, 0U));
static_assert(make_camera_matrix_identity(3U, 0U, 0U) !=
              make_camera_matrix_identity(3U, 0U, 1U));
static_assert(make_camera_matrix_identity(3U, 0U, 0U, 1U) !=
              make_camera_matrix_identity(3U, 0U, 0U, 2U));
static_assert(make_camera_matrix_identity(3U, 0U, 0U) !=
              kIgnoredIdentity);
static_assert(make_camera_matrix_identity(3U, 0U, 0U) !=
              kAutomaticIdentity);
constexpr auto kCameraEpoch1 = make_camera_continuity_identity(3U, 0U, 1U);
constexpr auto kCameraEpoch2 = make_camera_continuity_identity(3U, 0U, 2U);
constexpr auto kCameraViewport1 = make_camera_continuity_identity(3U, 1U, 1U);
static_assert(kCameraEpoch1 != kCameraEpoch2);
static_assert(kCameraEpoch1 != kCameraViewport1);
static_assert(with_camera_continuity(kObjectA, kCameraEpoch1) ==
              with_camera_continuity(kObjectA, kCameraEpoch1));
static_assert(with_camera_continuity(kObjectA, kCameraEpoch1) !=
              with_camera_continuity(kObjectA, kCameraEpoch2));
static_assert(with_camera_continuity(kObjectA, kCameraEpoch1) !=
              with_camera_continuity(kObjectA, kCameraViewport1));
static_assert(with_camera_continuity(kIgnoredIdentity, kCameraEpoch1) ==
              kIgnoredIdentity);
static_assert(make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 4U) ==
              make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 4U));
static_assert(make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 4U) !=
              make_wave_matrix_identity(3U, 1U, 0x100U, 1U, 2U, 3U, 4U));
static_assert(make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 4U) !=
              make_wave_matrix_identity(3U, 0U, 0x11CU, 1U, 2U, 3U, 4U));
static_assert(make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 4U) !=
              make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 5U));
static_assert(valid_wave_selection_pattern(0U));
static_assert(valid_wave_selection_pattern(9U));
static_assert(valid_wave_selection_pattern(10U));
static_assert(valid_wave_selection_pattern(25U));
static_assert(!valid_wave_selection_pattern(26U));
static_assert(!valid_wave_selection_pattern(255U));
static_assert(make_wave_topology_variant(4U, false, 1U, 0x3F800000U) ==
              make_wave_topology_variant(4U, false, 7U, 0x3F800000U));
static_assert(make_wave_topology_variant(4U, true, 1U, 0x3F000000U) ==
              make_wave_topology_variant(4U, true, 25U, 0x3F000000U));
static_assert(make_wave_topology_variant(4U, true, 0U, 0x3F000000U) !=
              make_wave_topology_variant(4U, true, 7U, 0x3F000000U));
static_assert(make_wave_topology_variant(4U, true, 7U, 0x3F000000U) !=
              make_wave_topology_variant(5U, true, 7U, 0x3F000000U));
static_assert(make_vehicle_part_matrix_identity(kObjectA, 0x123400U) ==
              make_vehicle_part_matrix_identity(kObjectA, 0x123400U));
static_assert(make_vehicle_part_matrix_identity(kObjectA, 0x123400U) !=
              make_vehicle_part_matrix_identity(kObjectA, 0x123440U));
static_assert(make_vehicle_part_matrix_identity(kObjectA, 0x123400U) !=
              make_matrix_identity(kObjectA, 0U));
static_assert(make_shadow_group_identity(0U) == kIgnoredIdentity);
static_assert(make_shadow_group_identity(1U) != kIgnoredIdentity);
static_assert(make_shadow_group_identity(1U) != kAutomaticIdentity);
static_assert(make_shadow_group_identity(1U) !=
              make_shadow_group_identity(2U));
static_assert(make_shadow_group_identity(1U, 1U) ==
              make_shadow_group_identity(1U, 1U));
static_assert(make_shadow_group_identity(1U, 1U) !=
              make_shadow_group_identity(1U, 2U));
static_assert(make_vehicle_part_group_identity(0U) == kIgnoredIdentity);
static_assert(vehicle_part_attachment_slot(0x1000U, 0x1000U) == 0U);
static_assert(vehicle_part_attachment_slot(0x1000U, 0x1040U) == 1U);
static_assert(vehicle_part_attachment_slot(0x1000U, 0x17C0U) == 31U);
static_assert(vehicle_part_attachment_slot(0x1000U, 0x1800U) ==
              kInvalidVehiclePartSlot);
static_assert(vehicle_part_attachment_slot(0x1040U, 0x1000U) ==
              kInvalidVehiclePartSlot);
static_assert(vehicle_part_attachment_slot(0x1000U, 0x1010U) ==
              kInvalidVehiclePartSlot);
static_assert(vehicle_part_frame_variant(0U, 16U) == 0U);
static_assert(vehicle_part_frame_variant(255U, 16U) == 15U);
static_assert(vehicle_part_frame_variant(128U, 16U) == 8U);
static_assert(vehicle_part_frame_variant(128U, 0U) == 0U);
static_assert(make_vehicle_part_group_identity(1U) != kIgnoredIdentity);
static_assert(make_vehicle_part_group_identity(1U) != kAutomaticIdentity);
static_assert(make_vehicle_part_group_identity(1U) !=
              make_vehicle_part_group_identity(2U));
static_assert(make_vehicle_part_group_identity(1U) !=
              make_shadow_group_identity(1U));
static_assert(make_billboard_group_identity(0U) == kIgnoredIdentity);
static_assert(make_billboard_group_identity(1U, 3U) != kIgnoredIdentity);
static_assert(make_billboard_group_identity(1U, 3U) !=
              make_billboard_group_identity(1U, 4U));
static_assert(make_billboard_group_identity(1U, 3U) !=
              make_vehicle_part_group_identity(1U));
static_assert(make_surface_group_identity(0U, 0U) == kIgnoredIdentity);
static_assert(make_surface_group_identity(1U, 3U) != kIgnoredIdentity);
static_assert(make_surface_group_identity(1U, 3U) !=
              make_surface_group_identity(1U, 4U));
int main() {
    const CameraContinuitySample camera_origin{
        0.0F, 100.0F, 200.0F, 60.0F, 0, 0, 0};
    const CameraContinuitySample camera_smooth{
        24.0F, 105.0F, 180.0F, 62.0F, 0x0200, -0x0100, 0x0080};
    const CameraContinuitySample camera_teleport{
        2000.0F, 100.0F, 200.0F, 60.0F, 0, 0, 0};
    const CameraContinuitySample camera_hard_turn{
        0.0F, 100.0F, 200.0F, 60.0F, 0x4000, 0, 0};
    const CameraContinuitySample camera_fov_cut{
        0.0F, 100.0F, 200.0F, 90.0F, 0, 0, 0};
    assert(!camera_sample_discontinuous(camera_origin, camera_smooth));
    assert(camera_sample_discontinuous(camera_origin, camera_teleport));
    assert(camera_sample_discontinuous(camera_origin, camera_hard_turn));
    assert(camera_sample_discontinuous(camera_origin, camera_fov_cut));
    assert(camera_angle_distance(static_cast<std::int16_t>(0x7F00),
                                 static_cast<std::int16_t>(0x8100)) ==
           0x0200U);

    constexpr std::array<ShadowVertexSample, 4> previous{{
        {0.0F, 2.0F, 0.0F}, {10.0F, 2.0F, 0.0F},
        {10.0F, 2.0F, 10.0F}, {0.0F, 2.0F, 10.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> translated{{
        {4.0F, 2.0F, -3.0F}, {14.0F, 2.0F, -3.0F},
        {14.0F, 2.0F, 7.0F}, {4.0F, 2.0F, 7.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> reordered{{
        {14.0F, 2.0F, -3.0F}, {4.0F, 2.0F, -3.0F},
        {14.0F, 2.0F, 7.0F}, {4.0F, 2.0F, 7.0F},
    }};
    constexpr std::array<std::uint16_t, 1> batches{{4U}};
    assert(shadow_geometry_corresponds(previous, translated, batches,
                                       4.0F, -3.0F, 8.0F));
    assert(!shadow_geometry_corresponds(previous, reordered, batches,
                                        4.0F, -3.0F, 16.0F));
    std::puts("[test][presentation-identity] PASS");
    return 0;
}
