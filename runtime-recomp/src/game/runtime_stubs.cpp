#include "recomp.h"

#include "runtime_enhancements.hpp"
#include "f3ddkr_rt64.hpp"
#include "runtime_platform.hpp"
#include "widescreen_policy.hpp"

#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"

#if DKR_RUNTIME_HAS_RT64
#include <SDL.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace {

#if DKR_RUNTIME_HAS_RT64
constexpr std::uint32_t kOrthoMatrixAddress = 0x800DD2B8U;
constexpr std::uint32_t kViewProjectionMatrixAddress = 0x80120F20U;
constexpr std::uint32_t kPresentationGroupCommand = 0xBC0000FEU;
constexpr std::uint32_t kPresentationGroupMagic = 0x444B5200U;
constexpr std::uint32_t kWaveFlagsPointerAddress = 0x800E30D4U;
constexpr std::uint32_t kWaveModelPointerAddress = 0x800E30D8U;
constexpr std::uint32_t kWavePatternShapesAddress = 0x800E3144U;
constexpr std::uint32_t kWaveTileTotalAddress = 0x800E317CU;
constexpr std::uint32_t kSceneActiveCameraAddress = 0x8011B0B0U;
constexpr std::uint32_t kVoidLateralXAddress = 0x8011D4A0U;
constexpr std::uint32_t kVoidLateralZAddress = 0x8011D4A4U;
constexpr std::uint32_t kVoidCentreXAddress = 0x8011D4ACU;
constexpr std::uint32_t kVoidCentreZAddress = 0x8011D4B0U;
constexpr std::uint32_t kWaveControllerAddress = 0x80129FC8U;
constexpr std::uint32_t kWavePlayerCountAddress = 0x8012A078U;
constexpr std::uint32_t kWaveBoundingBoxDiffXAddress = 0x8012A0A8U;
constexpr std::uint32_t kWaveBoundingBoxDiffZAddress = 0x8012A0ACU;
constexpr std::uint32_t kWaveTileCountXAddress = 0x8012A0D8U;
constexpr std::uint32_t kWaveTileCountZAddress = 0x8012A0DCU;
constexpr std::uint32_t kWaveSegmentCountAddress = 0x8012A0E0U;
constexpr std::uint32_t kWaveValidRowsAddress = 0x8012A0E8U;
constexpr std::uint32_t kWaveSelectionRecordsAddress = 0x8012A5E8U;
constexpr int kWaveSelectionCapacity = 25;
constexpr int kWaveSelectionRecordCount = 26;
constexpr int kWaveSelectionRecordStride = 12;
constexpr int kWaveModelStride = 0x1C;
constexpr std::array<std::int16_t, kWaveSelectionRecordCount>
    kOriginalWavePatternShapes = {
        0, 1, 1, 1, 2,
        3, 4, 4, 4, 5,
        3, 4, 4, 4, 5,
        3, 4, 4, 4, 5,
        6, 7, 7, 7, 8,
        0,
    };
constexpr float kOriginalAspect = 4.0F / 3.0F;
enum class PresentationGroupMode : std::uint32_t {
    World = 0U,
    StaticAuto = 1U,
    BackgroundFillStretch = 3U,
};
float g_saved_transition_x = 1.0F;
float g_saved_transition_y = 1.0F;
bool g_transition_cover_active = false;
bool g_transition_interpolation_active = false;
bool g_background_fill_stretch_active = false;
std::array<float, 8> g_saved_sky_projection_columns{};
bool g_sky_cover_active = false;
std::atomic<bool> g_logged_transition_cover{false};
std::atomic<bool> g_logged_gradient_cover{false};
std::atomic<bool> g_logged_menu_background_cover{false};
std::atomic<bool> g_logged_sky_cover{false};
std::atomic<bool> g_logged_wave_footprint{false};
std::atomic<bool> g_logged_void_cover{false};
std::atomic<bool> g_logged_background_fill_stretch{false};
std::atomic<bool> g_logged_postrace_viewport{false};

float ExpandedCoverScale() {
    using ultramodern::renderer::AspectRatio;
    if (ultramodern::renderer::get_graphics_config().ar_option != AspectRatio::Expand) {
        return 1.0F;
    }
    auto* window = static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window());
    if (window == nullptr) {
        return 1.0F;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return 1.0F;
    }
    return std::max(1.0F,
        (static_cast<float>(width) / static_cast<float>(height)) / kOriginalAspect);
}

float ReadRdramFloat(std::uint8_t* rdram, std::uint32_t address) {
    const auto signed_address = static_cast<gpr>(static_cast<std::int32_t>(address));
    return std::bit_cast<float>(static_cast<std::uint32_t>(MEM_W(0, signed_address)));
}

void WriteRdramFloat(std::uint8_t* rdram, std::uint32_t address, float value) {
    const auto signed_address = static_cast<gpr>(static_cast<std::int32_t>(address));
    MEM_W(0, signed_address) = std::bit_cast<std::uint32_t>(value);
}

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

bool AppendPresentationGroupCommand(std::uint8_t* rdram,
                                    gpr display_list_pointer,
                                    PresentationGroupMode mode) {
    if (display_list_pointer == 0) {
        return false;
    }
    const std::uint32_t current = static_cast<std::uint32_t>(
        MEM_W(0, display_list_pointer));
    if (current < 0x80000000U || current > 0x807FFFF8U) {
        return false;
    }
    const gpr command = RdramAddress(current);
    MEM_W(0, command) = kPresentationGroupCommand;
    MEM_W(4, command) = kPresentationGroupMagic |
        static_cast<std::uint32_t>(mode);
    MEM_W(0, display_list_pointer) = current + 8U;
    return true;
}

#endif

std::atomic<std::uint64_t> g_scheduler_sp_handlers{0};
std::atomic<std::uint64_t> g_scheduler_dp_handlers{0};

} // namespace

extern "C" void dkr_scheduler_sp_handled(std::uint8_t*, recomp_context*) {
    // The host graphics/audio worker cannot safely publish a dependent DP edge
    // until DKR's scheduler has finished clearing and rescheduling the active
    // SP task. This hook is placed at the common exit of __scHandleRSP by the
    // Patch Pipeline and provides that exact acknowledgement.
    g_scheduler_sp_handlers.fetch_add(1, std::memory_order_relaxed);
    ultramodern::acknowledge_external_message_src(
        ultramodern::EventMessageSource::Sp);
}

extern "C" void dkr_scheduler_dp_handled(std::uint8_t*, recomp_context*) {
    g_scheduler_dp_handlers.fetch_add(1, std::memory_order_relaxed);
}

extern "C" std::uint64_t dkr_scheduler_sp_handler_count() {
    return g_scheduler_sp_handlers.load(std::memory_order_relaxed);
}

extern "C" std::uint64_t dkr_scheduler_dp_handler_count() {
    return g_scheduler_dp_handlers.load(std::memory_order_relaxed);
}

extern "C" void dkr_transition_cover_begin(std::uint8_t* rdram,
                                             recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    const float cover_zoom = ExpandedCoverScale();
    if (cover_zoom <= 1.0001F) {
        return;
    }
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        g_transition_interpolation_active = AppendPresentationGroupCommand(
            rdram, context->r16, PresentationGroupMode::StaticAuto);
    }
    g_saved_transition_x = ReadRdramFloat(rdram, kOrthoMatrixAddress);
    g_saved_transition_y = ReadRdramFloat(rdram, kOrthoMatrixAddress + 5U * sizeof(float));
    WriteRdramFloat(rdram, kOrthoMatrixAddress, g_saved_transition_x * cover_zoom);
    WriteRdramFloat(rdram, kOrthoMatrixAddress + 5U * sizeof(float),
                    g_saved_transition_y * cover_zoom);
    g_transition_cover_active = true;
    if (!g_logged_transition_cover.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr, "[boot][widescreen] transition cover zoom=%.3f\n",
                     cover_zoom);
    }
#else
    (void)rdram;
#endif
}

extern "C" void dkr_widen_gradient_sky(std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    const float scale = ExpandedCoverScale();
    if (scale <= 1.0001F) {
        return;
    }
    // trackbg_render_gradient has just populated four 10-byte Vertex records;
    // r3 still points at the first record. Expand only their X coordinates so
    // the colour backdrop covers a wide viewport without touching the HUD,
    // track geometry, camera FOV, or its vertical gradient.
    for (gpr offset : {gpr{0}, gpr{10}, gpr{20}, gpr{30}}) {
        const std::int16_t x = static_cast<std::int16_t>(MEM_H(offset, context->r3));
        const long widened = std::lround(static_cast<float>(x) * scale);
        MEM_H(offset, context->r3) = static_cast<std::int16_t>(
            std::clamp(widened, -32768L, 32767L));
    }
    if (!g_logged_gradient_cover.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr, "[boot][widescreen] gradient background cover scale=%.3f\n",
                     scale);
    }
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_transition_cover_end(std::uint8_t* rdram, recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_transition_cover_active) {
        return;
    }
    WriteRdramFloat(rdram, kOrthoMatrixAddress, g_saved_transition_x);
    WriteRdramFloat(rdram, kOrthoMatrixAddress + 5U * sizeof(float), g_saved_transition_y);
    g_transition_cover_active = false;
#else
    (void)rdram;
#endif
}

extern "C" void dkr_transition_interpolation_end(std::uint8_t* rdram,
                                                   recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (g_transition_interpolation_active) {
        AppendPresentationGroupCommand(
            rdram, context->r16, PresentationGroupMode::World);
        g_transition_interpolation_active = false;
    }
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_background_fill_stretch_begin(std::uint8_t* rdram,
                                                     recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_background_fill_stretch_active = false;
    if (ExpandedCoverScale() <= 1.0001F) {
        return;
    }
    g_background_fill_stretch_active = AppendPresentationGroupCommand(
        rdram, context->r16, PresentationGroupMode::BackgroundFillStretch);
    if (g_background_fill_stretch_active &&
        !g_logged_background_fill_stretch.exchange(true,
                                                    std::memory_order_relaxed)) {
        std::fprintf(stderr,
                     "[boot][widescreen] gameplay background fill stretch active\n");
    }
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_background_fill_stretch_end(std::uint8_t* rdram,
                                                   recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_background_fill_stretch_active) {
        return;
    }
    AppendPresentationGroupCommand(rdram, context->r16,
                                   PresentationGroupMode::World);
    g_background_fill_stretch_active = false;
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_reshape_wave_footprint(std::uint8_t* rdram,
                                             recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    // Always restore the original 5x5 edge-shape lookup first. This makes a
    // live switch back to 4:3, split-screen, or a non-Expand mode exact rather
    // than leaving a widescreen frame's reshaped boundary data behind.
    for (int i = 0; i < kWaveSelectionRecordCount; ++i) {
        MEM_H(static_cast<gpr>(i * sizeof(std::int16_t)),
              RdramAddress(kWavePatternShapesAddress)) =
            kOriginalWavePatternShapes[static_cast<std::size_t>(i)];
    }

    const float cover_scale = ExpandedCoverScale();
    const int player_count = static_cast<int>(MEM_W(
        0, RdramAddress(kWavePlayerCountAddress)));
    const int view_distance = static_cast<int>(MEM_W(
        0x24, RdramAddress(kWaveControllerAddress)));
    if (cover_scale <= 1.0001F || player_count == 2 || view_distance != 5) {
        return;
    }

    const bool double_density = MEM_W(
        0x28, RdramAddress(kWaveControllerAddress)) != 0;
    const int density = double_density ? 2 : 1;
    const int subdivisions = static_cast<int>(MEM_W(
        0, RdramAddress(kWaveControllerAddress)));
    const int repeating_tile_count = static_cast<int>(MEM_W(
        4, RdramAddress(kWaveControllerAddress)));
    const int tile_count_x = static_cast<int>(MEM_W(
        0, RdramAddress(kWaveTileCountXAddress)));
    const int tile_count_z = static_cast<int>(MEM_W(
        0, RdramAddress(kWaveTileCountZAddress)));
    const int segment_count = static_cast<int>(MEM_W(
        0, RdramAddress(kWaveSegmentCountAddress)));
    const int diff_x = static_cast<int>(MEM_W(
        0, RdramAddress(kWaveBoundingBoxDiffXAddress)));
    const int diff_z = static_cast<int>(MEM_W(
        0, RdramAddress(kWaveBoundingBoxDiffZAddress)));
    const int wave_tile_total = static_cast<int>(MEM_W(
        0, RdramAddress(kWaveTileTotalAddress)));
    const std::uint32_t flags_vram = static_cast<std::uint32_t>(MEM_W(
        0, RdramAddress(kWaveFlagsPointerAddress)));
    const std::uint32_t model_vram = static_cast<std::uint32_t>(MEM_W(
        0, RdramAddress(kWaveModelPointerAddress)));
    const std::uint32_t camera_vram = static_cast<std::uint32_t>(MEM_W(
        0, RdramAddress(kSceneActiveCameraAddress)));
    if (subdivisions <= 0 || repeating_tile_count <= 0 ||
        tile_count_x <= 0 || tile_count_x > 32 ||
        tile_count_z <= 0 || tile_count_z > 64 ||
        segment_count <= 0 || segment_count > 512 || diff_x <= 0 || diff_z <= 0 ||
        wave_tile_total <= 0 || flags_vram < 0x80000000U ||
        flags_vram > 0x807FFFFCU || model_vram < 0x80000000U ||
        model_vram > 0x807FFFFCU || camera_vram < 0x80000000U ||
        camera_vram > 0x807FFFFCU) {
        return;
    }

    const gpr flags_address = RdramAddress(flags_vram);
    const gpr model_address = RdramAddress(model_vram);
    int original_start_x = 0;
    int original_start_z = 0;
    bool found_origin = false;
    for (int tile_z = 0; tile_z < tile_count_z && !found_origin; ++tile_z) {
        for (int tile_x = 0; tile_x < tile_count_x && !found_origin; ++tile_x) {
            const std::uint32_t flags = static_cast<std::uint32_t>(MEM_W(
                static_cast<gpr>((tile_z * tile_count_x + tile_x) * 4),
                flags_address));
            for (int sub_z = 0; sub_z < density && !found_origin; ++sub_z) {
                for (int sub_x = 0; sub_x < density; ++sub_x) {
                    const int shift = sub_x * 8 + sub_z * 16;
                    const int pattern = static_cast<int>((flags >> shift) & 0xFFU);
                    if (pattern <= 0 || pattern > kWaveSelectionCapacity) {
                        continue;
                    }
                    original_start_x = tile_x * density + sub_x -
                        ((pattern - 1) % view_distance);
                    original_start_z = tile_z * density + sub_z -
                        ((pattern - 1) / view_distance);
                    found_origin = true;
                    break;
                }
            }
        }
    }
    if (!found_origin) {
        return;
    }

    constexpr int kMaxWaveGridCells = 32 * 64;
    std::array<std::int16_t, kMaxWaveGridCells> segment_for_tile{};
    segment_for_tile.fill(-1);
    for (int segment = 0; segment < segment_count; ++segment) {
        const gpr entry = model_address +
            static_cast<gpr>(segment * kWaveModelStride);
        const int tile_index = static_cast<int>(MEM_W(0x0C, entry));
        if (tile_index >= 0 && tile_index < tile_count_x * tile_count_z) {
            segment_for_tile[static_cast<std::size_t>(tile_index)] =
                static_cast<std::int16_t>(segment);
        }
    }

    struct Candidate {
        int cell_x = 0;
        int cell_z = 0;
        int tile_x = 0;
        int tile_z = 0;
        int sub_x = 0;
        int sub_z = 0;
        int segment = -1;
        float score = 0.0F;
    };
    std::array<Candidate, 121> candidates{};
    int candidate_count = 0;
    const int centre_x = original_start_x + view_distance / 2;
    const int centre_z = original_start_z + view_distance / 2;
    // Vec3s stores DKR rotations in y/x/z order. Camera yaw is therefore the
    // first halfword, not offset 4 (which is roll). Reading roll made the old
    // widescreen selector expand a fixed world axis instead of the camera's
    // horizontal axis, so the visible HQ/LQ water boundary stayed 4:3.
    const auto camera_yaw = static_cast<std::int16_t>(MEM_H(
        0, RdramAddress(camera_vram)));
    constexpr float kTwoPi = 6.28318530717958647692F;
    const float yaw = -static_cast<float>(camera_yaw) *
        (kTwoPi / 65536.0F);
    const float yaw_sin = std::sin(yaw);
    const float yaw_cos = std::cos(yaw);
    const float step_x = static_cast<float>(diff_x) / static_cast<float>(density);
    const float step_z = static_cast<float>(diff_z) / static_cast<float>(density);
    const float normaliser = std::max(1.0F, (step_x + step_z) * 0.5F);
    const float lateral_scale = std::clamp(cover_scale, 1.0F, 2.667F);

    // Rank valid water sub-cells in camera space. Compressing the lateral
    // score by the host aspect widens the finite 25-cell footprint while the
    // unchanged forward score preserves the original five-cell depth. A small
    // forward bias spends vacated rear-corner slots where the camera can see.
    for (int cell_z = centre_z - 5; cell_z <= centre_z + 5; ++cell_z) {
        for (int cell_x = centre_x - 5; cell_x <= centre_x + 5; ++cell_x) {
            if (cell_x < 0 || cell_z < 0 ||
                cell_x >= tile_count_x * density ||
                cell_z >= tile_count_z * density) {
                continue;
            }
            const int tile_x = cell_x / density;
            const int tile_z = cell_z / density;
            const std::uint32_t valid_row = static_cast<std::uint32_t>(MEM_W(
                static_cast<gpr>(tile_z * 4),
                RdramAddress(kWaveValidRowsAddress)));
            if ((valid_row & (1U << tile_x)) == 0U) {
                continue;
            }
            const int tile_index = tile_z * tile_count_x + tile_x;
            const int segment = segment_for_tile[static_cast<std::size_t>(tile_index)];
            if (segment < 0 || candidate_count >= static_cast<int>(candidates.size())) {
                continue;
            }
            const float world_x = static_cast<float>(cell_x - centre_x) * step_x;
            const float world_z = static_cast<float>(cell_z - centre_z) * step_z;
            const float lateral = (world_x * yaw_cos - world_z * yaw_sin) /
                normaliser;
            const float forward = (world_x * yaw_sin + world_z * yaw_cos) /
                normaliser;
            const float scaled_lateral = lateral / lateral_scale;
            const float biased_forward = forward - 0.35F;
            candidates[static_cast<std::size_t>(candidate_count++)] = {
                cell_x, cell_z, tile_x, tile_z,
                cell_x % density, cell_z % density, segment,
                scaled_lateral * scaled_lateral +
                    biased_forward * biased_forward,
            };
        }
    }
    if (candidate_count <= 0) {
        return;
    }
    std::sort(candidates.begin(), candidates.begin() + candidate_count,
              [centre_x, centre_z](const Candidate& lhs, const Candidate& rhs) {
                  if (std::abs(lhs.score - rhs.score) > 0.0001F) {
                      return lhs.score < rhs.score;
                  }
                  const int lhs_dx = lhs.cell_x - centre_x;
                  const int lhs_dz = lhs.cell_z - centre_z;
                  const int rhs_dx = rhs.cell_x - centre_x;
                  const int rhs_dz = rhs.cell_z - centre_z;
                  const int lhs_distance = lhs_dx * lhs_dx + lhs_dz * lhs_dz;
                  const int rhs_distance = rhs_dx * rhs_dx + rhs_dz * rhs_dz;
                  if (lhs_distance != rhs_distance) {
                      return lhs_distance < rhs_distance;
                  }
                  if (lhs.cell_z != rhs.cell_z) {
                      return lhs.cell_z < rhs.cell_z;
                  }
                  return lhs.cell_x < rhs.cell_x;
              });
    const int selection_count = std::min(candidate_count, kWaveSelectionCapacity);

    for (int tile = 0; tile < tile_count_x * tile_count_z; ++tile) {
        MEM_W(static_cast<gpr>(tile * 4), flags_address) = 0;
    }
    for (int record = 0; record < kWaveSelectionRecordCount; ++record) {
        MEM_H(static_cast<gpr>(record * kWaveSelectionRecordStride),
              RdramAddress(kWaveSelectionRecordsAddress)) = -1;
    }

    const auto is_selected = [&](int x, int z) {
        for (int i = 0; i < selection_count; ++i) {
            if (candidates[static_cast<std::size_t>(i)].cell_x == x &&
                candidates[static_cast<std::size_t>(i)].cell_z == z) {
                return true;
            }
        }
        return false;
    };

    for (int index = 0; index < selection_count; ++index) {
        const Candidate& candidate = candidates[static_cast<std::size_t>(index)];
        const int slot = index + 1;
        const int shift = candidate.sub_x * 8 + candidate.sub_z * 16;
        const gpr flag_offset = static_cast<gpr>(
            (candidate.tile_z * tile_count_x + candidate.tile_x) * 4);
        std::uint32_t flags = static_cast<std::uint32_t>(MEM_W(
            flag_offset, flags_address));
        flags |= static_cast<std::uint32_t>(slot) << shift;
        MEM_W(flag_offset, flags_address) = flags;

        const bool missing_left = !is_selected(candidate.cell_x - 1, candidate.cell_z);
        const bool missing_right = !is_selected(candidate.cell_x + 1, candidate.cell_z);
        const bool missing_top = !is_selected(candidate.cell_x, candidate.cell_z - 1);
        const bool missing_bottom = !is_selected(candidate.cell_x, candidate.cell_z + 1);
        int edge_shape = 4;
        if (missing_top && missing_left) edge_shape = 0;
        else if (missing_top && missing_right) edge_shape = 2;
        else if (missing_bottom && missing_left) edge_shape = 6;
        else if (missing_bottom && missing_right) edge_shape = 8;
        else if (missing_top) edge_shape = 1;
        else if (missing_bottom) edge_shape = 7;
        else if (missing_left) edge_shape = 3;
        else if (missing_right) edge_shape = 5;
        MEM_H(static_cast<gpr>((slot - 1) * sizeof(std::int16_t)),
              RdramAddress(kWavePatternShapesAddress)) = edge_shape;

        const gpr model = model_address +
            static_cast<gpr>(candidate.segment * kWaveModelStride);
        const gpr record = RdramAddress(kWaveSelectionRecordsAddress) +
            static_cast<gpr>(index * kWaveSelectionRecordStride);
        int wave_u = static_cast<std::int16_t>(MEM_H(0x12, model));
        int wave_v = static_cast<std::int16_t>(MEM_H(0x10, model));
        int vertex_offset = candidate.segment * wave_tile_total;
        if (double_density && candidate.sub_x != 0) {
            vertex_offset += subdivisions;
            wave_u = (wave_u + subdivisions) % repeating_tile_count;
        }
        if (double_density && candidate.sub_z != 0) {
            vertex_offset += ((subdivisions * 2) + 1) * subdivisions;
            wave_v = (wave_v + subdivisions) % repeating_tile_count;
        }
        MEM_H(0, record) = candidate.segment;
        MEM_H(2, record) = double_density
            ? candidate.sub_x + candidate.sub_z * 2 : 0;
        MEM_H(4, record) = wave_u;
        MEM_H(6, record) = wave_v;
        MEM_W(8, record) = vertex_offset;
    }

    if (!g_logged_wave_footprint.exchange(true, std::memory_order_relaxed)) {
        int min_x = candidates[0].cell_x;
        int max_x = candidates[0].cell_x;
        int min_z = candidates[0].cell_z;
        int max_z = candidates[0].cell_z;
        for (int i = 1; i < selection_count; ++i) {
            min_x = std::min(min_x, candidates[static_cast<std::size_t>(i)].cell_x);
            max_x = std::max(max_x, candidates[static_cast<std::size_t>(i)].cell_x);
            min_z = std::min(min_z, candidates[static_cast<std::size_t>(i)].cell_z);
            max_z = std::max(max_z, candidates[static_cast<std::size_t>(i)].cell_z);
        }
        std::fprintf(stderr,
                     "[boot][widescreen] HQ wave footprint reshaped "
                     "slots=%d span=%dx%d density=%d cover=%.3f yaw=%d\n",
                     selection_count, max_x - min_x + 1, max_z - min_z + 1,
                     density, cover_scale, static_cast<int>(camera_yaw));
    }
#else
    (void)rdram;
#endif
}

extern "C" void dkr_widen_void_primitive(std::uint8_t* rdram,
                                            recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    const float cover_scale = ExpandedCoverScale();
    if (cover_scale <= 1.0001F) {
        return;
    }

    // void_generate_primitive has just written four vertices and advanced v0
    // by their 40-byte total. DKR's void is the flat-colour, camera-facing
    // mesh behind level geometry; its original +/-300 lateral limit covers a
    // 4:3 projection only. Expand solely along its own camera-horizontal basis
    // while preserving every vertex's Y coordinate and forward depth.
    const gpr vertices = context->r2 - static_cast<gpr>(4 * 10);
    const float lateral_x = ReadRdramFloat(rdram, kVoidLateralXAddress);
    const float lateral_z = ReadRdramFloat(rdram, kVoidLateralZAddress);
    const float centre_x = ReadRdramFloat(rdram, kVoidCentreXAddress);
    const float centre_z = ReadRdramFloat(rdram, kVoidCentreZAddress);
    const float basis_length_sq =
        lateral_x * lateral_x + lateral_z * lateral_z;
    if (!std::isfinite(lateral_x) || !std::isfinite(lateral_z) ||
        !std::isfinite(centre_x) || !std::isfinite(centre_z) ||
        basis_length_sq < 0.5F || basis_length_sq > 1.5F) {
        return;
    }

    for (std::uint32_t index = 0; index < 4U; ++index) {
        const gpr vertex = vertices + static_cast<gpr>(index * 10U);
        const float x = static_cast<float>(
            static_cast<std::int16_t>(MEM_H(0, vertex)));
        const float z = static_cast<float>(
            static_cast<std::int16_t>(MEM_H(4, vertex)));
        const float dx = x - centre_x;
        const float dz = z - centre_z;
        const float lateral =
            (dx * lateral_x + dz * lateral_z) / basis_length_sq;
        const float expansion = lateral * (cover_scale - 1.0F);
        const long widened_x = std::clamp(
            std::lround(x + expansion * lateral_x), -32768L, 32767L);
        const long widened_z = std::clamp(
            std::lround(z + expansion * lateral_z), -32768L, 32767L);
        MEM_H(0, vertex) = static_cast<std::int16_t>(widened_x);
        MEM_H(4, vertex) = static_cast<std::int16_t>(widened_z);
    }

    if (!g_logged_void_cover.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr,
                     "[boot][widescreen] lower-horizon void cover scale=%.3f\n",
                     cover_scale);
    }
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_track_select_background_cover(std::uint8_t* rdram,
                                                     recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    const float cover_zoom = ExpandedCoverScale();
    if (cover_zoom <= 1.0001F || context->r8 == 0) {
        return;
    }

    // func_8008F618 has finished populating one private four-vertex strip and
    // restored its base pointer to t0/r8. Widen only those background vertices.
    // Writing absolute coordinates makes this idempotent across frames and
    // avoids mutating gOrthoMatrixF or gViewProjMatrixF, which are shared by
    // subsequent menu, HUD, and world draws.
    const long half_width = std::clamp(
        std::lround(160.0F * cover_zoom), 160L, 32767L);
    for (std::uint32_t vertex = 0; vertex < 4U; ++vertex) {
        const long x = ((vertex & 1U) == 0U) ? -half_width : half_width;
        MEM_H(static_cast<gpr>(vertex * 10U), context->r8) =
            static_cast<std::int16_t>(x);
    }

    if (!g_logged_menu_background_cover.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr,
                     "[boot][widescreen] track-select background half-width=%ld\n",
                     half_width);
    }
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_expand_postrace_viewport(std::uint8_t*,
                                                recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!dkr::runtime::enhancements::modern_presentation_enabled() ||
        !dkr::runtime::enhancements::fit_to_window_enabled()) {
        return;
    }
    const float cover = ExpandedCoverScale();
    if (cover <= 1.0001F) {
        return;
    }

    // The post-race camera deliberately shrinks vertically, but its original
    // 0..320 horizontal viewport/scissor exposes a 4:3 boundary when RT64 is
    // presenting an expanded frame. Extend only the viewport edges into the
    // additional host width. Menu elements and race-result HUD coordinates are
    // left on the authored 4:3 canvas.
    context->r5 = static_cast<gpr>(
        dkr::runtime::enhancements::expanded_postrace_left(cover));
    context->r7 = static_cast<gpr>(
        dkr::runtime::enhancements::expanded_postrace_right(cover));
    if (!g_logged_postrace_viewport.exchange(true,
                                              std::memory_order_relaxed)) {
        std::fprintf(stderr,
                     "[boot][widescreen] post-race viewport x=%d..%d "
                     "cover=%.3f\n",
                     static_cast<std::int32_t>(context->r5),
                     static_cast<std::int32_t>(context->r7), cover);
    }
#else
    (void)context;
#endif
}

extern "C" void dkr_skybox_cover_begin(std::uint8_t* rdram,
                                         recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    const float cover_zoom = ExpandedCoverScale();
    if (cover_zoom <= 1.0001F || g_sky_cover_active) {
        return;
    }

    // This hook runs after mtx_world_origin() has rebuilt the current camera
    // projection and immediately before render_object() builds skydome-only
    // matrices. Preserve the accepted uniform cover through 21:9. At wider
    // ratios, invert only the excess vertical zoom so the horizon moves back
    // down while horizontal coverage continues to reach the viewport edges.
    const float vertical_zoom =
        dkr::runtime::enhancements::sky_vertical_cover_scale(cover_zoom);
    for (std::uint32_t row = 0; row < 4U; ++row) {
        for (std::uint32_t column = 0; column < 2U; ++column) {
            const std::uint32_t slot = row * 2U + column;
            const std::uint32_t address = kViewProjectionMatrixAddress +
                (row * 4U + column) * sizeof(float);
            g_saved_sky_projection_columns[slot] = ReadRdramFloat(rdram, address);
            WriteRdramFloat(rdram, address,
                            g_saved_sky_projection_columns[slot] *
                                (column == 0U ? cover_zoom : vertical_zoom));
        }
    }
    g_sky_cover_active = true;
    if (!g_logged_sky_cover.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr,
                     "[boot][widescreen] skydome cover x=%.3f y=%.3f\n",
                     cover_zoom, vertical_zoom);
    }
#else
    (void)rdram;
#endif
}

extern "C" void dkr_skybox_cover_end(std::uint8_t* rdram, recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_sky_cover_active) {
        return;
    }
    for (std::uint32_t row = 0; row < 4U; ++row) {
        for (std::uint32_t column = 0; column < 2U; ++column) {
            const std::uint32_t slot = row * 2U + column;
            const std::uint32_t address = kViewProjectionMatrixAddress +
                (row * 4U + column) * sizeof(float);
            WriteRdramFloat(rdram, address, g_saved_sky_projection_columns[slot]);
        }
    }
    g_sky_cover_active = false;
#else
    (void)rdram;
#endif
}

extern "C" int dkr_audio_voice_guard(std::uint8_t* rdram, recomp_context* context) {
    const auto synth_address = static_cast<std::uint32_t>(context->r4);
    const auto voice_address = static_cast<std::uint32_t>(context->r5);
    const auto requested_bus = static_cast<std::uint16_t>(context->r6);
    const auto is_rdram_address = [](std::uint32_t address) {
        return address >= 0x80000000U && address < 0x80800000U;
    };
    if (!is_rdram_address(synth_address) || !is_rdram_address(voice_address) ||
        requested_bus > 1U) {
        std::fprintf(stderr,
                     "[boot][audio] ignored invalid voice route synth=%08X voice=%08X bus=%u\n",
                     synth_address, voice_address, static_cast<unsigned>(requested_bus));
        return 1;
    }

    const auto read_word = [rdram](std::uint32_t address, std::uint32_t offset) {
        const auto signed_address = static_cast<gpr>(static_cast<std::int32_t>(address));
        return static_cast<std::uint32_t>(MEM_W(offset, signed_address));
    };
    bool found_voice = false;
    for (const std::uint32_t list_offset : {0x04U, 0x0CU, 0x14U}) {
        std::uint32_t node = read_word(synth_address, list_offset);
        for (unsigned count = 0; node != 0 && count < 128; ++count) {
            if (!is_rdram_address(node)) {
                break;
            }
            if (node == voice_address) {
                found_voice = true;
                break;
            }
            node = read_word(node, 0);
        }
        if (found_voice) {
            break;
        }
    }
    if (!found_voice) {
        std::fprintf(stderr,
                     "[boot][audio] ignored voice outside synth lists synth=%08X voice=%08X bus=%u\n",
                     synth_address, voice_address, static_cast<unsigned>(requested_bus));
        return 1;
    }

    const auto signed_voice = static_cast<gpr>(static_cast<std::int32_t>(voice_address));
    const auto current_bus = static_cast<std::uint32_t>(MEM_BU(0xDC, signed_voice));
    if (current_bus > 1U) {
        std::fprintf(stderr,
                     "[boot][audio] normalized uninitialized voice bus voice=%08X value=%u\n",
                     voice_address, current_bus);
        MEM_B(0xDC, signed_voice) = 0;
    }
    return 0;
}

extern "C" int dkr_audio_bus_guard(std::uint8_t* rdram, recomp_context* context) {
    const auto bus_address = static_cast<std::uint32_t>(context->r4);
    const auto is_rdram_address = [](std::uint32_t address) {
        return address >= 0x80000000U && address < 0x80800000U;
    };

    if (!is_rdram_address(bus_address)) {
        std::fprintf(stderr,
                     "[boot][audio] ignored invalid auxiliary bus=%08X param=%u source=%08X\n",
                     bus_address, static_cast<unsigned>(context->r5),
                     static_cast<unsigned>(context->r6));
        context->r2 = 0;
        return 1;
    }

    const auto signed_bus = static_cast<gpr>(static_cast<std::int32_t>(bus_address));
    const auto source_count = static_cast<std::uint32_t>(MEM_W(0x14, signed_bus));
    const auto sources_address = static_cast<std::uint32_t>(MEM_W(0x1C, signed_bus));
    if (source_count > 64U || !is_rdram_address(sources_address)) {
        std::fprintf(stderr,
                     "[boot][audio] ignored malformed auxiliary bus=%08X param=%u "
                     "count=%u sources=%08X\n",
                     bus_address, static_cast<unsigned>(context->r5), source_count,
                     sources_address);
        context->r2 = 0;
        return 1;
    }
    return 0;
}

extern "C" void rmonPrintf_recomp(std::uint8_t*, recomp_context*) {
    // Retail debug output has no observable game-state effect.
}

extern "C" void __osSpSetStatus_recomp(std::uint8_t*, recomp_context*) {
    // SP task state is owned by ultramodern's scheduler.
}

extern "C" void __osSiGetAccess_recomp(std::uint8_t*, recomp_context*) {}
extern "C" void __osSiRelAccess_recomp(std::uint8_t*, recomp_context*) {}
