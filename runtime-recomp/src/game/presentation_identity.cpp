#include "presentation_identity.hpp"

#include "recomp.h"

#include "runtime_enhancements.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "win95/sync.hpp"

namespace {

constexpr std::uint32_t kRdramMask = 0x007FFFFFU;
constexpr std::uint32_t kObjectBehaviourOffset = 0x48U;
constexpr std::uint32_t kObjectIdOffset = 0x4AU;
constexpr std::uint32_t kObjectCurrentMatrixAddress = 0x8011AE90U;
constexpr std::uint32_t kSpTaskNumberAddress = 0x801234E8U;
constexpr std::uint32_t kCamerasAddress = 0x80120AC0U;
constexpr std::uint32_t kCameraSize = 0x44U;
constexpr std::uint32_t kActiveCameraIdAddress = 0x80120CE4U;
constexpr std::uint32_t kCurrentCameraFovAddress = 0x80120D10U;
constexpr std::uint32_t kCutsceneCameraActiveAddress = 0x80120D14U;
constexpr std::uint32_t kSceneActiveCameraAddress = 0x8011B0B0U;
constexpr std::uint32_t kCameraModeOffset = 0x36U;
constexpr std::uint32_t kWaveControllerAddress = 0x80129FC8U;
constexpr std::uint32_t kWaveSubdivisionsOffset = 0x00U;
constexpr std::uint32_t kWaveDoubleDensityOffset = 0x28U;
constexpr std::uint32_t kShadowHeapFlipAddress = 0x8011B0C8U;
constexpr std::uint32_t kShadowHeapTrianglesAddress = 0x8011D320U;
constexpr std::uint32_t kShadowHeapVerticesAddress = 0x8011D338U;
constexpr std::uint32_t kShadowHeapDataAddress = 0x8011D350U;
constexpr std::uint32_t kObjectHeaderOffset = 0x40U;
constexpr std::uint32_t kObjectHeaderShadowGroupOffset = 0x32U;
constexpr std::uint32_t kShadowMeshStartOffset = 0x08U;
constexpr std::uint32_t kShadowMeshEndOffset = 0x0AU;
constexpr std::uint32_t kShadowHeapPropertySize = 0x08U;
constexpr std::uint32_t kTriangleSize = 0x10U;
constexpr std::uint32_t kVertexSize = 0x0AU;
constexpr std::int32_t kMaximumShadowBatches = 400;
constexpr std::int32_t kMaximumShadowTriangles = 800;
constexpr std::int32_t kMaximumShadowVertices = 2000;
constexpr std::size_t kMaximumObjectNesting = 16U;
constexpr std::size_t kMaximumPendingFrames = 8U;

struct ShadowGeometrySnapshot {
    std::vector<dkr::runtime::presentation::ShadowVertexSample> vertices;
    std::vector<std::uint16_t> batch_vertex_counts;
    float object_x = 0.0F;
    float object_z = 0.0F;
    float footprint = 0.0F;
};

struct Lifetime {
    std::uint32_t generation = 0;
    std::uint16_t presentation_token = 0;
    std::uint64_t shadow_topology_signature = 0;
    std::uint8_t shadow_topology_epoch = 0;
    std::array<std::uint16_t, 32> vehicle_part_tokens{};
    ShadowGeometrySnapshot shadow_geometry{};
    bool shadow_topology_valid = false;
    bool alive = false;
};

struct ObjectCapture {
    std::uint32_t object = 0;
    std::uint32_t identity = 0;
    std::uint32_t camera_identity = 0;
    std::uint32_t first_matrix = 0;
    std::uint32_t buffer = 0;
    std::uint16_t presentation_token = 0;
};

struct ObjectOwner {
    std::uint32_t scene = 0;
    std::uint32_t address = 0;
    std::uint32_t lifetime = 0;

    bool operator==(const ObjectOwner&) const = default;
};

struct MatrixBinding {
    std::uint32_t matrix_identity = 0;
    std::uint32_t object_identity = 0;
    bool interpolate_vertices = false;
    bool interpolate_texcoords = false;
    bool interpolate_tiles = false;
};

struct SubmittedFrame {
    std::uint32_t display_list_address = 0;
    std::unordered_map<std::uint32_t, MatrixBinding> matrices;
    bool interpolation_allowed = false;
};

struct CameraContinuityState {
    dkr::runtime::presentation::CameraContinuitySample sample{};
    std::uint32_t scene = 0U;
    std::uint32_t task = 0U;
    std::uint32_t epoch = 1U;
    bool valid = false;
};

dkr::sync::mutex g_identity_mutex;
std::unordered_map<std::uint32_t, Lifetime> g_lifetimes;
std::unordered_map<std::uint32_t, ObjectOwner> g_identity_owners;
std::unordered_set<std::uint32_t> g_collided_identities;
std::array<std::unordered_map<std::uint32_t, MatrixBinding>, 2> g_matrix_maps;
std::deque<SubmittedFrame> g_submitted_frames;
bool g_submission_overflowed = false;
std::atomic<std::uint32_t> g_scene_generation{1U};
std::atomic<std::uint32_t> g_next_lifetime{1U};
std::uint32_t g_next_presentation_token = 1U;
std::atomic<std::uint64_t> g_identity_collisions{0U};
std::atomic<std::uint64_t> g_matrix_ranges{0U};
std::array<CameraContinuityState, 8> g_camera_continuity{};
std::array<bool, 8> g_camera_discontinuity_pending{};
thread_local std::array<ObjectCapture, kMaximumObjectNesting> g_capture_stack{};
thread_local std::size_t g_capture_depth = 0U;
thread_local std::size_t g_capture_overflow_depth = 0U;
thread_local std::uint32_t g_recording_buffer = 0U;
thread_local std::uint32_t g_current_camera_identity = 0U;
thread_local bool g_recording_interpolation_allowed = false;
thread_local bool g_active_task_interpolation_allowed = false;
thread_local std::unordered_map<std::uint32_t, MatrixBinding>
    g_active_matrix_map;
thread_local bool g_wave_capture_active = false;
thread_local std::uint32_t g_wave_capture_viewport = 0U;
thread_local std::uint32_t g_wave_block_address = 0U;
thread_local bool g_wave_block_valid = false;
thread_local std::uint8_t g_wave_selection_pattern = 0U;
thread_local bool g_wave_selection_valid = false;

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

bool ValidObjectAddress(std::uint32_t address) {
    return address >= 0x80000000U && address <= 0x807FFF00U;
}

std::uint32_t Physical(std::uint32_t address) {
    return address & kRdramMask;
}

std::uint32_t ReadU32(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::uint32_t>(MEM_W(0, RdramAddress(address)));
}

std::uint8_t ReadU8(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::uint8_t>(MEM_B(0, RdramAddress(address)));
}

std::uint16_t ReadU16(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::uint16_t>(MEM_H(0, RdramAddress(address)));
}

std::int16_t ReadS16(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::int16_t>(MEM_H(0, RdramAddress(address)));
}

float ReadF32(std::uint8_t* rdram, std::uint32_t address) {
    return std::bit_cast<float>(ReadU32(rdram, address));
}

bool ValidRange(std::uint32_t address, std::uint32_t size) {
    const std::uint32_t physical = Physical(address);
    return address >= 0x80000000U && address <= 0x807FFFFFU &&
        physical <= kRdramMask && size <= kRdramMask + 1U - physical;
}

int ActiveSceneCameraMode(std::uint8_t* rdram) {
    const std::uint32_t camera = ReadU32(rdram, kSceneActiveCameraAddress);
    if (!ValidRange(camera, kCameraSize)) {
        return -1;
    }
    return static_cast<int>(ReadS16(rdram, camera + kCameraModeOffset));
}

bool ActiveLogicalCamera(std::uint8_t* rdram, std::uint32_t& camera_id) {
    const std::uint32_t raw_camera_id =
        ReadU32(rdram, kActiveCameraIdAddress);
    const bool cutscene_camera =
        ReadU8(rdram, kCutsceneCameraActiveAddress) != 0U;
    if (raw_camera_id >= g_camera_continuity.size() ||
        (cutscene_camera && raw_camera_id >= 4U)) {
        return false;
    }
    camera_id = raw_camera_id + (cutscene_camera ? 4U : 0U);
    return camera_id < g_camera_continuity.size();
}

void HashTopologyValue(std::uint64_t& hash, std::uint32_t value) {
    // FNV-1a is sufficient here: the result is used only to notice that one
    // object's immediately preceding shadow layout is no longer compatible.
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        hash ^= (value >> shift) & 0xFFU;
        hash *= 1099511628211ULL;
    }
}

bool ShadowTopologySignature(std::uint8_t* rdram,
                             std::uint32_t object,
                             std::uint32_t shadow,
                             std::uint64_t& signature,
                             ShadowGeometrySnapshot& geometry) {
    if (!ValidObjectAddress(object) || !ValidRange(shadow, 0x10U)) {
        return false;
    }

    const std::int32_t mesh_start = ReadS16(
        rdram, shadow + kShadowMeshStartOffset);
    const std::int32_t mesh_end = ReadS16(
        rdram, shadow + kShadowMeshEndOffset);
    if (mesh_start < 0 || mesh_end <= mesh_start ||
        mesh_end > kMaximumShadowBatches) {
        return false;
    }

    const std::uint32_t header = ReadU32(
        rdram, object + kObjectHeaderOffset);
    if (!ValidRange(header + kObjectHeaderShadowGroupOffset, 2U)) {
        return false;
    }
    const std::int16_t shadow_group = ReadS16(
        rdram, header + kObjectHeaderShadowGroupOffset);
    std::int32_t heap_index = static_cast<std::int32_t>(ReadU32(
        rdram, kShadowHeapFlipAddress));
    if (shadow_group == 1) {
        heap_index += 2;
    }
    if (heap_index < 0 || heap_index >= 4) {
        return false;
    }

    const std::uint32_t heap_data = ReadU32(
        rdram, kShadowHeapDataAddress +
            static_cast<std::uint32_t>(heap_index) * 4U);
    const std::uint32_t heap_triangles = ReadU32(
        rdram, kShadowHeapTrianglesAddress +
            static_cast<std::uint32_t>(heap_index) * 4U);
    const std::uint32_t heap_vertices = ReadU32(
        rdram, kShadowHeapVerticesAddress +
            static_cast<std::uint32_t>(heap_index) * 4U);
    const std::uint32_t property_bytes =
        (static_cast<std::uint32_t>(mesh_end) + 1U) *
        kShadowHeapPropertySize;
    if (!ValidRange(heap_data, property_bytes) ||
        !ValidRange(heap_triangles,
                    kMaximumShadowTriangles * kTriangleSize) ||
        !ValidRange(heap_vertices,
                    kMaximumShadowVertices * kVertexSize)) {
        return false;
    }

    geometry = {};
    geometry.object_x = ReadF32(rdram, object + 0x0CU);
    geometry.object_z = ReadF32(rdram, object + 0x14U);
    geometry.footprint = std::abs(ReadF32(rdram, shadow)) * 10.0F;
    if (!std::isfinite(geometry.object_x) ||
        !std::isfinite(geometry.object_z) ||
        !std::isfinite(geometry.footprint)) {
        return false;
    }

    std::uint64_t hash = 1469598103934665603ULL;
    HashTopologyValue(hash,
        static_cast<std::uint32_t>(mesh_end - mesh_start));
    for (std::int32_t batch = mesh_start; batch < mesh_end; ++batch) {
        const std::uint32_t current = heap_data +
            static_cast<std::uint32_t>(batch) * kShadowHeapPropertySize;
        const std::uint32_t next = current + kShadowHeapPropertySize;
        const std::int32_t tri_start = ReadS16(rdram, current + 4U);
        const std::int32_t vert_start = ReadS16(rdram, current + 6U);
        const std::int32_t tri_end = ReadS16(rdram, next + 4U);
        const std::int32_t vert_end = ReadS16(rdram, next + 6U);
        if (tri_start < 0 || tri_end < tri_start ||
            tri_end > kMaximumShadowTriangles || vert_start < 0 ||
            vert_end < vert_start || vert_end > kMaximumShadowVertices) {
            return false;
        }

        // Absolute offsets depend on shadows generated earlier in this heap.
        // Hash only this object's batch shape and material, then the triangle
        // index topology. Moving coordinates and UV animation are deliberately
        // excluded so a compatible moving decal remains interpolated.
        HashTopologyValue(hash, ReadU32(rdram, current));
        HashTopologyValue(hash,
            static_cast<std::uint32_t>(tri_end - tri_start));
        HashTopologyValue(hash,
            static_cast<std::uint32_t>(vert_end - vert_start));
        const std::int32_t vertex_count = vert_end - vert_start;
        geometry.batch_vertex_counts.push_back(
            static_cast<std::uint16_t>(vertex_count));
        geometry.vertices.reserve(
            geometry.vertices.size() + static_cast<std::size_t>(vertex_count));
        for (std::int32_t vertex = vert_start; vertex < vert_end; ++vertex) {
            const std::uint32_t address = heap_vertices +
                static_cast<std::uint32_t>(vertex) * kVertexSize;
            geometry.vertices.push_back({
                static_cast<float>(ReadS16(rdram, address)),
                static_cast<float>(ReadS16(rdram, address + 2U)),
                static_cast<float>(ReadS16(rdram, address + 4U)),
            });
        }
        for (std::int32_t tri = tri_start; tri < tri_end; ++tri) {
            HashTopologyValue(hash, ReadU32(
                rdram, heap_triangles +
                    static_cast<std::uint32_t>(tri) * kTriangleSize));
        }
    }
    signature = hash;
    return true;
}

std::uint32_t NextLifetimeGeneration() {
    for (;;) {
        const std::uint32_t generation =
            g_next_lifetime.fetch_add(1U, std::memory_order_relaxed);
        if (generation != 0U) {
            return generation;
        }
    }
}

std::uint32_t EnsureLifetimeLocked(std::uint32_t object) {
    Lifetime& lifetime = g_lifetimes[object];
    if (!lifetime.alive || lifetime.generation == 0U) {
        lifetime = {};
        lifetime.generation = NextLifetimeGeneration();
        lifetime.presentation_token = g_next_presentation_token <= 0xFFFFU
            ? static_cast<std::uint16_t>(g_next_presentation_token++)
            : 0U;
        lifetime.alive = true;
    }
    return lifetime.generation;
}

std::uint32_t ObjectIdentityLocked(std::uint8_t* rdram,
                                   std::uint32_t object) {
    const std::uint32_t generation = EnsureLifetimeLocked(object);
    const std::uint16_t object_id = ReadU16(rdram, object + kObjectIdOffset);
    const std::uint16_t behaviour_id =
        ReadU16(rdram, object + kObjectBehaviourOffset);
    const std::uint32_t scene =
        g_scene_generation.load(std::memory_order_relaxed);
    const std::uint32_t identity =
        dkr::runtime::presentation::make_object_identity(
            scene, Physical(object), generation, object_id, behaviour_id);
    if (g_collided_identities.contains(identity)) {
        return dkr::runtime::presentation::kIgnoredIdentity;
    }
    const ObjectOwner owner{scene, Physical(object), generation};
    const auto [it, inserted] = g_identity_owners.emplace(identity, owner);
    if (!inserted && it->second != owner) {
        g_identity_collisions.fetch_add(1U, std::memory_order_relaxed);
        g_collided_identities.insert(identity);
        // Disable both owners of a collision, including bindings authored by
        // the first owner earlier in this frame.
        for (auto& map : g_matrix_maps) {
            std::erase_if(map, [identity](const auto& item) {
                return item.second.object_identity == identity;
            });
        }
        return dkr::runtime::presentation::kIgnoredIdentity;
    }
    return identity;
}

void NoteSpawn(std::uint32_t object) {
    if (!ValidObjectAddress(object)) {
        return;
    }
    dkr::sync::scoped_lock lock(g_identity_mutex);
    Lifetime& lifetime = g_lifetimes[object];
    lifetime = {};
    lifetime.generation = NextLifetimeGeneration();
    lifetime.presentation_token = g_next_presentation_token <= 0xFFFFU
        ? static_cast<std::uint16_t>(g_next_presentation_token++)
        : 0U;
    lifetime.alive = true;
}

void NoteFree(std::uint32_t object) {
    if (!ValidObjectAddress(object)) {
        return;
    }
    dkr::sync::scoped_lock lock(g_identity_mutex);
    const auto it = g_lifetimes.find(object);
    if (it != g_lifetimes.end()) {
        it->second.alive = false;
    }
}

void RegisterCameraMatrix(std::uint8_t* rdram,
                          std::uint32_t matrix_reference,
                          std::uint8_t matrix_role) {
    if (!dkr::runtime::enhancements::modern_presentation_enabled() ||
        !ValidRange(matrix_reference, 4U)) {
        return;
    }

    const std::uint32_t matrix_address = ReadU32(rdram, matrix_reference);
    if (!ValidRange(matrix_address, 64U)) {
        return;
    }

    dkr::sync::scoped_lock lock(g_identity_mutex);
    const std::uint32_t scene =
        g_scene_generation.load(std::memory_order_relaxed);
    std::uint32_t camera_id = 0U;
    if (!ActiveLogicalCamera(rdram, camera_id)) {
        g_matrix_maps[g_recording_buffer & 1U].insert_or_assign(
            Physical(matrix_address), MatrixBinding{});
        g_current_camera_identity =
            dkr::runtime::presentation::kIgnoredIdentity;
        return;
    }
    const std::uint32_t camera =
        kCamerasAddress + camera_id * kCameraSize;
    if (!ValidRange(camera, kCameraSize)) {
        g_matrix_maps[g_recording_buffer & 1U].insert_or_assign(
            Physical(matrix_address), MatrixBinding{});
        g_current_camera_identity =
            dkr::runtime::presentation::kIgnoredIdentity;
        return;
    }

    const std::uint16_t combined_pitch = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(ReadS16(rdram, camera + 0x02U)) +
        static_cast<std::uint16_t>(ReadS16(rdram, camera + 0x38U)));
    const dkr::runtime::presentation::CameraContinuitySample sample{
        ReadF32(rdram, camera + 0x0CU),
        ReadF32(rdram, camera + 0x10U),
        ReadF32(rdram, camera + 0x14U),
        ReadF32(rdram, kCurrentCameraFovAddress),
        ReadS16(rdram, camera + 0x00U),
        static_cast<std::int16_t>(combined_pitch),
        ReadS16(rdram, camera + 0x04U),
    };
    const std::uint32_t task = ReadU32(rdram, kSpTaskNumberAddress);
    CameraContinuityState& continuity = g_camera_continuity[camera_id];
    const bool forced_discontinuity =
        g_camera_discontinuity_pending[camera_id];
    g_camera_discontinuity_pending[camera_id] = false;
    if (!continuity.valid || continuity.scene != scene) {
        continuity = CameraContinuityState{sample, scene, task, 1U, true};
        if (forced_discontinuity) {
            ++continuity.epoch;
        }
    } else {
        // gSpTaskNumber counts submitted graphics tasks, not authored game
        // frames. DKR may emit more than one task for a frame, so a numeric
        // gap is not a camera discontinuity. Only the camera transform itself
        // is allowed to start a new interpolation epoch.
        const bool new_task = continuity.task != task;
        if (forced_discontinuity ||
            (new_task &&
             dkr::runtime::presentation::camera_sample_discontinuous(
                 continuity.sample, sample))) {
            ++continuity.epoch;
            if (continuity.epoch == 0U) {
                continuity.epoch = 1U;
            }
        }
        if (new_task || forced_discontinuity) {
            continuity.sample = sample;
            continuity.task = task;
        }
    }

    g_current_camera_identity =
        dkr::runtime::presentation::make_camera_continuity_identity(
            scene, camera_id, continuity.epoch);

    const std::uint32_t identity =
        dkr::runtime::presentation::make_camera_matrix_identity(
            scene, camera_id, matrix_role, continuity.epoch);
    g_matrix_maps[g_recording_buffer & 1U].insert_or_assign(
        Physical(matrix_address), MatrixBinding{identity, 0U, false, false});
}

} // namespace

dkr::runtime::presentation::MatrixInterpolation
dkr::runtime::presentation::matrix_interpolation(
    std::uint32_t physical_matrix_address) {
    const std::uint32_t address = physical_matrix_address & kRdramMask;
    const auto it = g_active_matrix_map.find(address);
    if (it != g_active_matrix_map.end()) {
        return {
            it->second.matrix_identity,
            it->second.interpolate_vertices,
            it->second.interpolate_texcoords,
            it->second.interpolate_tiles,
        };
    }
    return {};
}

std::uint32_t dkr::runtime::presentation::matrix_identity(
    std::uint32_t physical_matrix_address) {
    return matrix_interpolation(physical_matrix_address).identity;
}

dkr::runtime::presentation::PresentationKey
dkr::runtime::presentation::surface_presentation_key(
    std::uint32_t batch_address) {
    if (!ValidRange(batch_address, 0x0CU)) {
        return {};
    }
    std::uint32_t packed = normalise_identity(
        0x53555246U ^ Physical(batch_address) ^
        (g_scene_generation.load(std::memory_order_relaxed) * 0x9E3779B9U));
    packed &= 0x001FFFFFU;
    std::uint16_t token = static_cast<std::uint16_t>(packed & 0xFFFFU);
    if (token == 0U) {
        token = 1U;
    }
    return {token, static_cast<std::uint8_t>((packed >> 16U) & 0x1FU)};
}

std::uint16_t dkr::runtime::presentation::presentation_token_for_object(
    std::uint8_t*, std::uint32_t object_address) {
    if (!ValidObjectAddress(object_address)) {
        return 0U;
    }
    dkr::sync::scoped_lock lock(g_identity_mutex);
    EnsureLifetimeLocked(object_address);
    return g_lifetimes[object_address].presentation_token;
}

std::uint16_t
dkr::runtime::presentation::presentation_token_for_registered_object(
    std::uint8_t*, std::uint32_t object_address) {
    if (!ValidObjectAddress(object_address)) {
        return 0U;
    }
    dkr::sync::scoped_lock lock(g_identity_mutex);
    const auto it = g_lifetimes.find(object_address);
    return it != g_lifetimes.end() && it->second.alive
        ? it->second.presentation_token
        : 0U;
}

std::uint16_t
dkr::runtime::presentation::presentation_token_for_active_object(
    std::uint32_t object_address) {
    if (g_capture_depth == 0U || g_capture_overflow_depth != 0U) {
        return 0U;
    }
    const ObjectCapture& capture = g_capture_stack[g_capture_depth - 1U];
    return capture.object == object_address ? capture.presentation_token : 0U;
}

dkr::runtime::presentation::VehiclePartPresentationKey
dkr::runtime::presentation::active_vehicle_part_presentation_key(
    std::uint32_t attachment_matrix_address) {
    if (g_capture_depth == 0U || g_capture_overflow_depth != 0U) {
        return {};
    }
    const ObjectCapture& capture = g_capture_stack[g_capture_depth - 1U];
    if (capture.object == 0U || capture.presentation_token == 0U ||
        capture.first_matrix == 0U) {
        return {};
    }
    const std::uint8_t slot = vehicle_part_attachment_slot(
        capture.first_matrix, Physical(attachment_matrix_address));
    if (slot == kInvalidVehiclePartSlot) {
        return {};
    }
    dkr::sync::scoped_lock lock(g_identity_mutex);
    const auto lifetime_it = g_lifetimes.find(capture.object);
    if (lifetime_it == g_lifetimes.end() || !lifetime_it->second.alive) {
        return {};
    }
    std::uint16_t& attachment_token =
        lifetime_it->second.vehicle_part_tokens[slot];
    if (attachment_token == 0U && g_next_presentation_token <= 0xFFFFU) {
        attachment_token =
            static_cast<std::uint16_t>(g_next_presentation_token++);
    }
    return {attachment_token, slot};
}

std::uint32_t dkr::runtime::presentation::register_active_vehicle_part_matrix(
    std::uint32_t attachment_transform_address,
    std::uint32_t attachment_matrix_address) {
    if (g_capture_depth == 0U || g_capture_overflow_depth != 0U ||
        !ValidObjectAddress(attachment_transform_address) ||
        !ValidObjectAddress(attachment_matrix_address)) {
        return kIgnoredIdentity;
    }
    const ObjectCapture& capture = g_capture_stack[g_capture_depth - 1U];
    const std::uint32_t matrix = Physical(attachment_matrix_address);
    const std::uint8_t slot = vehicle_part_attachment_slot(
        capture.first_matrix, matrix);
    if (capture.identity == kIgnoredIdentity ||
        slot == kInvalidVehiclePartSlot) {
        return kIgnoredIdentity;
    }

    const std::uint32_t identity = make_vehicle_part_matrix_identity(
        capture.identity, Physical(attachment_transform_address));
    dkr::sync::scoped_lock lock(g_identity_mutex);
    g_matrix_maps[capture.buffer & 1U].insert_or_assign(
        matrix, MatrixBinding{
            with_camera_continuity(identity, capture.camera_identity),
            capture.identity, false, false});
    return identity;
}

dkr::runtime::presentation::ShadowPresentationKey
dkr::runtime::presentation::shadow_presentation_key(
    std::uint8_t* rdram, std::uint32_t object_address,
    std::uint32_t shadow_address) {
    std::uint64_t topology_signature = 0U;
    ShadowGeometrySnapshot geometry{};
    if (!ShadowTopologySignature(rdram, object_address, shadow_address,
                                 topology_signature, geometry)) {
        return {};
    }

    dkr::sync::scoped_lock lock(g_identity_mutex);
    EnsureLifetimeLocked(object_address);
    Lifetime& lifetime = g_lifetimes[object_address];
    bool incompatible_geometry = false;
    if (lifetime.shadow_topology_valid &&
        lifetime.shadow_topology_signature == topology_signature) {
        const float footprint = std::max(
            geometry.footprint, lifetime.shadow_geometry.footprint);
        const float maximum_residual = std::max(6.0F, footprint * 0.5F);
        incompatible_geometry = !shadow_geometry_corresponds(
            lifetime.shadow_geometry.vertices, geometry.vertices,
            geometry.batch_vertex_counts,
            geometry.object_x - lifetime.shadow_geometry.object_x,
            geometry.object_z - lifetime.shadow_geometry.object_z,
            maximum_residual);
    }

    if (!lifetime.shadow_topology_valid) {
        lifetime.shadow_topology_epoch = 1U;
        lifetime.shadow_topology_valid = true;
    } else if (lifetime.shadow_topology_signature != topology_signature ||
               incompatible_geometry) {
        // Only five bits travel in DKR's otherwise-unused custom command byte.
        // A change always advances by one, so it cannot match the immediately
        // preceding authored frame even when the counter eventually wraps.
        lifetime.shadow_topology_epoch = static_cast<std::uint8_t>(
            (lifetime.shadow_topology_epoch + 1U) & 0x1FU);
    }
    lifetime.shadow_topology_signature = topology_signature;
    lifetime.shadow_geometry = std::move(geometry);
    return {lifetime.presentation_token, lifetime.shadow_topology_epoch};
}

dkr::runtime::presentation::TaskIdentityScope::TaskIdentityScope(
    std::uint32_t display_list_address) {
    g_active_matrix_map.clear();
    g_active_task_interpolation_allowed = false;
    dkr::sync::scoped_lock lock(g_identity_mutex);
    if (g_submitted_frames.empty()) {
        return;
    }
    SubmittedFrame frame = std::move(g_submitted_frames.front());
    g_submitted_frames.pop_front();
    const std::uint32_t expected = frame.display_list_address & kRdramMask;
    const std::uint32_t actual = display_list_address & kRdramMask;
    if (expected != actual) {
        std::fprintf(stderr,
                     "[boot][presentation] identity sidecar task mismatch "
                     "expected=0x%06X actual=0x%06X; interpolation disabled "
                     "for this task\n",
                     expected, actual);
        return;
    }
    g_active_matrix_map = std::move(frame.matrices);
    g_active_task_interpolation_allowed = frame.interpolation_allowed;
}

dkr::runtime::presentation::TaskIdentityScope::~TaskIdentityScope() {
    g_active_matrix_map.clear();
    g_active_task_interpolation_allowed = false;
}

bool dkr::runtime::presentation::task_interpolation_allowed() {
    return g_active_task_interpolation_allowed;
}

extern "C" void dkr_presentation_scene_begin(std::uint8_t*, recomp_context*) {
    dkr::sync::scoped_lock lock(g_identity_mutex);
    std::uint32_t next =
        g_scene_generation.fetch_add(1U, std::memory_order_relaxed) + 1U;
    if (next == 0U) {
        g_scene_generation.store(1U, std::memory_order_relaxed);
    }
    g_lifetimes.clear();
    g_identity_owners.clear();
    g_collided_identities.clear();
    g_next_presentation_token = 1U;
    g_camera_continuity = {};
    g_camera_discontinuity_pending = {};
    g_current_camera_identity =
        dkr::runtime::presentation::kIgnoredIdentity;
    g_recording_interpolation_allowed = false;
    g_active_task_interpolation_allowed = false;
    g_wave_capture_active = false;
    g_wave_capture_viewport = 0U;
    g_wave_block_address = 0U;
    g_wave_block_valid = false;
    g_submission_overflowed = false;
    for (auto& map : g_matrix_maps) {
        map.clear();
        map.reserve(1024U);
    }
}

extern "C" void dkr_presentation_frame_begin(std::uint8_t* rdram,
                                              recomp_context*) {
    g_recording_buffer = ReadU32(rdram, kSpTaskNumberAddress) & 1U;
    g_capture_depth = 0U;
    g_capture_overflow_depth = 0U;
    g_current_camera_identity =
        dkr::runtime::presentation::kIgnoredIdentity;
    const int camera_mode = ActiveSceneCameraMode(rdram);
    g_recording_interpolation_allowed =
        dkr::runtime::enhancements::interpolation_allowed_for_camera(
            dkr::runtime::enhancements::presentation_profile(), camera_mode);
    dkr::sync::scoped_lock lock(g_identity_mutex);
    g_matrix_maps[g_recording_buffer].clear();
}

extern "C" void dkr_presentation_viewport_camera_mode(
    std::uint8_t* rdram, recomp_context*) {
    const int camera_mode = ActiveSceneCameraMode(rdram);
    g_recording_interpolation_allowed =
        dkr::runtime::enhancements::interpolation_allowed_for_camera(
            dkr::runtime::enhancements::presentation_profile(), camera_mode);
    if (!g_recording_interpolation_allowed) {
        g_current_camera_identity =
            dkr::runtime::presentation::kIgnoredIdentity;
    }
}

extern "C" void dkr_presentation_perspective_matrix(
    std::uint8_t* rdram, recomp_context* context) {
    // At 0x80068108, a0 has been restored to the caller's Mtx ** and still
    // points at the matrix just written by mtx_perspective.
    RegisterCameraMatrix(rdram, static_cast<std::uint32_t>(context->r4), 0U);
}

extern "C" void dkr_presentation_world_origin_matrix(
    std::uint8_t* rdram, recomp_context* context) {
    // At 0x800684D8, s0 still contains the caller's Mtx ** and has not yet
    // advanced past the matrix produced by mtx_world_origin.
    RegisterCameraMatrix(rdram, static_cast<std::uint32_t>(context->r16), 1U);
}

extern "C" void dkr_presentation_wave_begin(std::uint8_t* rdram,
                                               recomp_context* context) {
    g_wave_capture_active =
        dkr::runtime::enhancements::modern_presentation_enabled();
    g_wave_capture_viewport = static_cast<std::uint32_t>(context->r6);
    g_wave_block_address = 0U;
    g_wave_block_valid = false;
    g_wave_selection_pattern = 0U;
    g_wave_selection_valid = false;

}

extern "C" void dkr_presentation_wave_end(std::uint8_t*, recomp_context*) {
    g_wave_capture_active = false;
    g_wave_capture_viewport = 0U;
    g_wave_block_address = 0U;
    g_wave_block_valid = false;
    g_wave_selection_pattern = 0U;
    g_wave_selection_valid = false;
}

extern "C" void dkr_presentation_wave_block(std::uint8_t*,
                                              recomp_context* context) {
    if (!g_wave_capture_active) {
        return;
    }

    // At 0x800BAE30 in waves_render, v0/r2 still holds the current
    // WaveBlockModel pointer before execution branches to the normal- or
    // double-density draw path. The model entry is stable for the lifetime of
    // the scene, unlike the alternating wave vertex buffers and matrix heap.
    const std::uint32_t block_address =
        static_cast<std::uint32_t>(context->r2);
    g_wave_block_valid = ValidRange(block_address, 0x1CU);
    g_wave_block_address =
        g_wave_block_valid ? Physical(block_address) : 0U;
}

extern "C" void dkr_presentation_wave_selection(
    std::uint8_t* rdram, recomp_context* context) {
    if (!g_wave_capture_active) {
        return;
    }

    // This hook runs in waves_render immediately before each of its two
    // static mtx_cam_push call sites. The caller's 0x120-byte frame is still
    // current here, so 0x104(sp) is the decompiled local `sp104`: a packed
    // sequence of grid selectors in the inclusive range 0..25. At
    // mtx_cam_push entry, by contrast, $sp belongs to the callee and this
    // offset is unrelated stack data.
    const std::uint32_t packed_selection =
        static_cast<std::uint32_t>(MEM_W(0x104, context->r29));
    const std::uint8_t selection_pattern =
        static_cast<std::uint8_t>(packed_selection & 0xFFU);

    g_wave_selection_valid =
        dkr::runtime::presentation::valid_wave_selection_pattern(
            selection_pattern);
    g_wave_selection_pattern = selection_pattern;
}

extern "C" void dkr_presentation_wave_matrix(
    std::uint8_t* rdram, recomp_context* context) {
    if (!g_wave_capture_active) {
        return;
    }

    // mtx_cam_push receives Mtx ** in a1 and ObjectTransform * in a2. This
    // hook runs at function entry, before either argument is overwritten.
    const std::uint32_t matrix_reference =
        static_cast<std::uint32_t>(context->r5);
    const std::uint32_t transform = static_cast<std::uint32_t>(context->r6);
    if (!ValidRange(matrix_reference, 4U) || !ValidRange(transform, 0x10U)) {
        return;
    }
    const std::uint32_t matrix_address = ReadU32(rdram, matrix_reference);
    if (!ValidRange(matrix_address, 64U)) {
        return;
    }

    const std::uint32_t subdivisions = ReadU32(
        rdram, kWaveControllerAddress + kWaveSubdivisionsOffset);
    const bool double_density = ReadU32(
        rdram, kWaveControllerAddress + kWaveDoubleDensityOffset);
    // The caller-side hook captured waves_render's `sp104` before this call.
    // Consume it exactly once so an unrelated mtx_cam_push can never reuse a
    // stale selector. If the call-site hook was missed or the decompiled
    // invariant is violated, retain the authored matrix without registering
    // an unsafe interpolation identity.
    const bool selection_valid = g_wave_selection_valid;
    const std::uint8_t selection_pattern = g_wave_selection_pattern;
    g_wave_selection_pattern = 0U;
    g_wave_selection_valid = false;
    if (!selection_valid || !g_wave_block_valid) {
        return;
    }
    const std::uint32_t scale_bits = ReadU32(rdram, transform + 0x0CU);
    const std::uint32_t topology =
        dkr::runtime::presentation::make_wave_topology_variant(
            subdivisions, double_density, selection_pattern, scale_bits);
    const std::uint32_t identity =
        dkr::runtime::presentation::make_wave_matrix_identity(
        g_scene_generation.load(std::memory_order_relaxed),
        g_wave_capture_viewport,
        g_wave_block_address,
        ReadU32(rdram, transform + 0x00U),
        ReadU32(rdram, transform + 0x04U),
        ReadU32(rdram, transform + 0x08U), topology);

    dkr::sync::scoped_lock lock(g_identity_mutex);
    g_matrix_maps[g_recording_buffer & 1U].insert_or_assign(
        Physical(matrix_address),
        MatrixBinding{
            dkr::runtime::presentation::with_camera_continuity(
                identity, g_current_camera_identity),
            0U, true, true, true});
}

extern "C" void dkr_presentation_object_spawned(std::uint8_t*,
                                                 recomp_context* context) {
    NoteSpawn(static_cast<std::uint32_t>(context->r2));
}

extern "C" void dkr_presentation_object_freed(std::uint8_t*,
                                               recomp_context* context) {
    NoteFree(static_cast<std::uint32_t>(context->r4));
}

extern "C" void dkr_presentation_task_submitted(std::uint8_t*,
                                                  recomp_context* context) {
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    SubmittedFrame frame{};
    frame.display_list_address = static_cast<std::uint32_t>(context->r4);
    frame.interpolation_allowed = g_recording_interpolation_allowed;
    dkr::sync::scoped_lock lock(g_identity_mutex);
    if (g_submission_overflowed) {
        return;
    }
    if (g_submitted_frames.size() >= kMaximumPendingFrames) {
        g_submitted_frames.clear();
        g_submission_overflowed = true;
        std::fprintf(stderr,
                     "[boot][presentation] semantic sidecar queue exceeded "
                     "%zu tasks; interpolation identities disabled until "
                     "the next scene\n",
                     kMaximumPendingFrames);
        return;
    }
    frame.matrices = std::move(g_matrix_maps[g_recording_buffer & 1U]);
    g_matrix_maps[g_recording_buffer & 1U].reserve(1024U);
    g_submitted_frames.emplace_back(std::move(frame));
}

extern "C" void dkr_presentation_object_begin(std::uint8_t* rdram,
                                               recomp_context* context) {
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    if (g_capture_depth >= g_capture_stack.size()) {
        ++g_capture_overflow_depth;
        return;
    }
    ObjectCapture& capture = g_capture_stack[g_capture_depth++];
    capture = {};
    capture.buffer = g_recording_buffer;
    const std::uint32_t object = static_cast<std::uint32_t>(context->r7);
    if (!ValidObjectAddress(object)) {
        return;
    }
    const gpr matrix_reference = MEM_W(context->r29, 0x24);
    const std::uint32_t first_matrix =
        static_cast<std::uint32_t>(MEM_W(0, matrix_reference));
    if (!ValidObjectAddress(first_matrix)) {
        return;
    }
    dkr::sync::scoped_lock lock(g_identity_mutex);
    capture.object = object;
    capture.identity = ObjectIdentityLocked(rdram, object);
    capture.camera_identity = g_current_camera_identity;
    capture.first_matrix = Physical(first_matrix);
    const auto lifetime = g_lifetimes.find(object);
    capture.presentation_token =
        lifetime != g_lifetimes.end() && lifetime->second.alive
            ? lifetime->second.presentation_token
            : 0U;
}

extern "C" void dkr_presentation_object_end(std::uint8_t* rdram,
                                             recomp_context*) {
    if (g_capture_overflow_depth != 0U) {
        --g_capture_overflow_depth;
        return;
    }
    if (g_capture_depth == 0U) {
        return;
    }
    const ObjectCapture capture = g_capture_stack[--g_capture_depth];
    const std::uint32_t end_matrix =
        Physical(ReadU32(rdram, kObjectCurrentMatrixAddress));
    if (capture.identity == dkr::runtime::presentation::kIgnoredIdentity ||
        end_matrix < capture.first_matrix ||
        ((end_matrix - capture.first_matrix) & 0x3FU) != 0U) {
        return;
    }
    const std::uint32_t matrix_count =
        (end_matrix - capture.first_matrix) / 64U;
    if (matrix_count == 0U || matrix_count > 256U) {
        return;
    }

    dkr::sync::scoped_lock lock(g_identity_mutex);
    auto& map = g_matrix_maps[capture.buffer & 1U];
    for (std::uint32_t ordinal = 0; ordinal < matrix_count; ++ordinal) {
        const std::uint32_t address = capture.first_matrix + ordinal * 64U;
        // A nested render_object completes first. Preserve its more-specific
        // ownership when the outer object's wider range is closed.
        map.try_emplace(address, MatrixBinding{
            dkr::runtime::presentation::with_camera_continuity(
                dkr::runtime::presentation::make_matrix_identity(
                    capture.identity, ordinal),
                capture.camera_identity),
            capture.identity, false, false});
    }
    g_matrix_ranges.fetch_add(1U, std::memory_order_relaxed);
}

extern "C" void dkr_presentation_finish_camera_enter(
    std::uint8_t*, recomp_context*) {
    // This hook lands on the exact transition into CAMERA_FINISH_RACE. The
    // post-race flag can be committed later in the same authored frame, so
    // gate the recording immediately. Do not churn camera epochs for every
    // fixed spectator node: that created expensive pairing invalidations and
    // still allowed incompatible end-race display lists to meet.
    g_recording_interpolation_allowed = false;
    g_current_camera_identity =
        dkr::runtime::presentation::kIgnoredIdentity;
}
