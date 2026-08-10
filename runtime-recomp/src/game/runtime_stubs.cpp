#include "recomp.h"

#include "runtime_enhancements.hpp"
#include "f3ddkr_rt64.hpp"
#include "intro_tail_policy.hpp"
#include "presentation_identity.hpp"
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
constexpr std::uint32_t kTrackDisplayListAddress = 0x8011B0A0U;
constexpr std::uint32_t kPresentationGroupCommand = 0xBC0000FEU;
constexpr std::uint32_t kPresentationGroupMagic = 0x444B5200U;
constexpr std::uint32_t kVoidLateralXAddress = 0x8011D4A0U;
constexpr std::uint32_t kVoidLateralZAddress = 0x8011D4A4U;
constexpr std::uint32_t kVoidCentreXAddress = 0x8011D4ACU;
constexpr std::uint32_t kVoidCentreZAddress = 0x8011D4B0U;
constexpr float kOriginalAspect = 4.0F / 3.0F;
enum class PresentationGroupMode : std::uint32_t {
    World = 0U,
    StaticAuto = 1U,
    DynamicShadow = 2U,
    BackgroundFillStretch = 3U,
    DynamicVehiclePart = 4U,
    DynamicBillboard = 6U,
    DynamicSurface = 7U,
};
float g_saved_transition_x = 1.0F;
float g_saved_transition_y = 1.0F;
bool g_transition_cover_active = false;
bool g_transition_interpolation_active = false;
bool g_background_fill_stretch_active = false;
bool g_postrace_background_stretch_active = false;
bool g_chequer_background_stretch_active = false;
bool g_shadow_interpolation_active = false;
bool g_vehicle_part_interpolation_active = false;
bool g_billboard_interpolation_active = false;
bool g_surface_interpolation_active = false;
dkr::runtime::intro::TailGate g_title_intro_tail_gate{};
std::array<float, 8> g_saved_sky_projection_columns{};
bool g_sky_cover_active = false;

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
                                    PresentationGroupMode mode,
                                    std::uint16_t token = 0U,
                                    std::uint8_t variant = 0U) {
    if (display_list_pointer == 0) {
        return false;
    }
    const std::uint32_t current = static_cast<std::uint32_t>(
        MEM_W(0, display_list_pointer));
    if (current < 0x80000000U || current > 0x807FFFF8U) {
        return false;
    }
    const gpr command = RdramAddress(current);
    MEM_W(0, command) = kPresentationGroupCommand |
        (static_cast<std::uint32_t>(token) << 8U);
    MEM_W(4, command) = kPresentationGroupMagic |
        ((static_cast<std::uint32_t>(variant) & 0x1FU) << 3U) |
        static_cast<std::uint32_t>(mode);
    MEM_W(0, display_list_pointer) = current + 8U;
    return true;
}

#endif

std::atomic<std::uint64_t> g_scheduler_sp_late_events{0};

bool IsRdramWordAddress(std::uint32_t address, std::uint32_t final_offset) {
    // libultra passes both KSEG0 pointers and, in a few low-level paths,
    // physical RDRAM offsets. Reject every other segment before MEM_W masks
    // the address so a stale host completion cannot alias arbitrary memory.
    const bool valid_segment =
        address <= 0x007FFFFCU ||
        (address >= 0x80000000U && address <= 0x807FFFFCU) ||
        (address >= 0xA0000000U && address <= 0xA07FFFFCU);
    if (!valid_segment) {
        return false;
    }
    const std::uint32_t physical = address & 0x1FFFFFFFU;
    return final_offset <= 0x007FFFFCU &&
        physical <= 0x007FFFFCU - final_offset;
}

} // namespace

extern "C" int dkr_scheduler_sp_event_valid(std::uint8_t* rdram,
                                               recomp_context* context) {
    if (rdram != nullptr && context != nullptr) {
        const std::uint32_t scheduler =
            static_cast<std::uint32_t>(context->r4);
        if (IsRdramWordAddress(scheduler, 0x274U)) {
            const std::uint32_t task = static_cast<std::uint32_t>(
                MEM_W(0x274, static_cast<gpr>(
                    static_cast<std::int32_t>(scheduler))));
            // __scHandleRSP's first task access is task + 0x10. A null task
            // means the scheduler already consumed this SP edge; it is not a
            // valid retail task and must not reach that unchecked load.
            if (IsRdramWordAddress(task, 0x10U)) {
                return 1;
            }
        }
    }

    const std::uint64_t late_event =
        g_scheduler_sp_late_events.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (late_event <= 8U) {
        const std::uint32_t scheduler = context != nullptr
            ? static_cast<std::uint32_t>(context->r4)
            : 0U;
        std::uint32_t task = 0U;
        if (rdram != nullptr && IsRdramWordAddress(scheduler, 0x274U)) {
            task = static_cast<std::uint32_t>(MEM_W(
                0x274,
                static_cast<gpr>(static_cast<std::int32_t>(scheduler))));
        }
        std::fprintf(stderr,
                     "[boot][scheduler] dropped late SP completion "
                     "scheduler=%08X task=%08X count=%llu\n",
                     scheduler, task,
                     static_cast<unsigned long long>(late_event));
    }

    // The normal acknowledgement lives at __scHandleRSP's common exit. This
    // early-return path must publish the same acknowledgement exactly once or
    // the host worker can remain blocked behind the discarded completion.
    ultramodern::acknowledge_external_message_src(
        ultramodern::EventMessageSource::Sp);
    return 0;
}

extern "C" void dkr_scheduler_sp_handled(std::uint8_t*, recomp_context*) {
    // The host graphics/audio worker cannot safely publish a dependent DP edge
    // until DKR's scheduler has finished clearing and rescheduling the active
    // SP task. This hook is placed at the common exit of __scHandleRSP by the
    // Patch Pipeline and provides that exact acknowledgement.
    ultramodern::acknowledge_external_message_src(
        ultramodern::EventMessageSource::Sp);
}

extern "C" void dkr_scheduler_dp_handled(std::uint8_t*, recomp_context*) {
}

extern "C" void dkr_shadow_interpolation_begin(std::uint8_t* rdram,
                                                  recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_shadow_interpolation_active = false;
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    const std::uint32_t object = static_cast<std::uint32_t>(context->r6);
    const std::uint32_t shadow = static_cast<std::uint32_t>(context->r7);
    const auto key = dkr::runtime::presentation::shadow_presentation_key(
        rdram, object, shadow);
    const PresentationGroupMode mode = key.token != 0U
        ? PresentationGroupMode::DynamicShadow
        : PresentationGroupMode::StaticAuto;
    g_shadow_interpolation_active = AppendPresentationGroupCommand(
        rdram, RdramAddress(kTrackDisplayListAddress), mode, key.token,
        key.topology_epoch);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_vehicle_part_interpolation_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_vehicle_part_interpolation_active = false;
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }

    // This branch has already replaced s0 with DKR's single shared transform
    // scratch buffer, so s0 is deliberately *not* an object identity. Pair the
    // attachment with its active parent render_object and the ordinal of the
    // private matrix that DKR is about to submit. This keeps four wheels,
    // propellers and steering wheels distinct while allowing directional
    // sprite frames to change discretely (vehicle-part vertices are never
    // interpolated by the F3DDKR bridge).
    const std::uint32_t matrix_reference =
        static_cast<std::uint32_t>(context->r6);
    if (matrix_reference < 0x80000000U || matrix_reference > 0x807FFFFCU) {
        return;
    }
    const std::uint32_t attachment_matrix = static_cast<std::uint32_t>(
        MEM_W(0, static_cast<gpr>(static_cast<std::int32_t>(
                     matrix_reference))));
    const auto key =
        dkr::runtime::presentation::active_vehicle_part_presentation_key(
            attachment_matrix);
    if (key.attachment_token == 0U ||
        key.attachment_slot ==
            dkr::runtime::presentation::kInvalidVehiclePartSlot) {
        return;
    }
    const std::uint32_t sprite = static_cast<std::uint32_t>(
        MEM_W(0x70, context->r29));
    if (sprite < 0x80000000U || sprite > 0x807FFFFCU) {
        return;
    }
    const std::uint16_t frame_count = static_cast<std::uint16_t>(
        MEM_H(0, static_cast<gpr>(static_cast<std::int32_t>(sprite))));
    const std::uint8_t frame_variant =
        dkr::runtime::presentation::vehicle_part_frame_variant(
            static_cast<std::uint32_t>(context->r18), frame_count);
    g_vehicle_part_interpolation_active = AppendPresentationGroupCommand(
        rdram, context->r17, PresentationGroupMode::DynamicVehiclePart,
        key.attachment_token, frame_variant);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_vehicle_part_matrix_identity(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    const std::uint32_t transform = static_cast<std::uint32_t>(context->r16);
    const std::uint32_t matrix_reference = static_cast<std::uint32_t>(
        MEM_W(0x64, context->r29));
    if (matrix_reference < 0x80000000U || matrix_reference > 0x807FFFFCU) {
        return;
    }
    const std::uint32_t matrix = static_cast<std::uint32_t>(
        MEM_W(0, RdramAddress(matrix_reference)));
    dkr::runtime::presentation::register_active_vehicle_part_matrix(
        transform, matrix);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_billboard_interpolation_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_billboard_interpolation_active = false;
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }

    // This Patch-Pipeline boundary exists only on render_sprite_billboard's
    // ordinary 3D branch, after the world-space anchor has been authored and
    // immediately before its vertex command. Restrict interpolation to real
    // top-level registered game Objects. Vehicle attachments (including the
    // car steering wheel) enter this function while their parent render_object
    // capture is active and use a temporary parent-relative transform. Giving
    // those children an independent billboard group makes RT64 pair that
    // temporary pose as if it were a world object and can discard the draw.
    // Weather scratch transforms, HUD records and other temporary
    // Object-shaped data likewise retain the safe retail path.
    const std::uint32_t object = static_cast<std::uint32_t>(context->r16);
    const std::uint16_t token =
        dkr::runtime::presentation::presentation_token_for_active_object(object);
    const std::uint32_t sprite = static_cast<std::uint32_t>(
        MEM_W(0x70, context->r29));
    if (token == 0U || sprite == 0U) {
        return;
    }

    const std::uint8_t variant =
        dkr::runtime::presentation::presentation_variant_for_address(sprite);
    g_billboard_interpolation_active = AppendPresentationGroupCommand(
        rdram, context->r17, PresentationGroupMode::DynamicBillboard,
        token, variant);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_vehicle_part_interpolation_end(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_vehicle_part_interpolation_active &&
        !g_billboard_interpolation_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, context->r17, PresentationGroupMode::World);
    g_vehicle_part_interpolation_active = false;
    g_billboard_interpolation_active = false;
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_shadow_interpolation_end(std::uint8_t* rdram,
                                                recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_shadow_interpolation_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, RdramAddress(kTrackDisplayListAddress),
        PresentationGroupMode::World);
    g_shadow_interpolation_active = false;
#else
    (void)rdram;
#endif
}

extern "C" void dkr_surface_interpolation_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_surface_interpolation_active = false;
    constexpr std::uint32_t kRenderWater = 0x2000U;
    if (!dkr::runtime::enhancements::modern_presentation_enabled() ||
        (static_cast<std::uint32_t>(context->r17) & kRenderWater) == 0U) {
        return;
    }
    const auto key = dkr::runtime::presentation::surface_presentation_key(
        static_cast<std::uint32_t>(context->r5));
    if (key.token == 0U) {
        return;
    }
    g_surface_interpolation_active = AppendPresentationGroupCommand(
        rdram, RdramAddress(kTrackDisplayListAddress),
        PresentationGroupMode::DynamicSurface, key.token, key.variant);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_surface_interpolation_end(
    std::uint8_t* rdram, recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_surface_interpolation_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, RdramAddress(kTrackDisplayListAddress),
        PresentationGroupMode::World);
    g_surface_interpolation_active = false;
#else
    (void)rdram;
#endif
}

extern "C" void dkr_title_intro_audio_tail(
    std::uint8_t* rdram, recomp_context* context) {
    constexpr std::uint32_t kTitleDemoIndexAddress = 0x80126864U;
    constexpr std::uint32_t kTitleRevealTimerAddress = 0x8012686CU;
    const bool cinematic_complete = context->r2 != 0;
    const bool first_title_demo =
        MEM_W(0, RdramAddress(kTitleDemoIndexAddress)) == 0;
    const bool title_revealed =
        MEM_W(0, RdramAddress(kTitleRevealTimerAddress)) != 0;
    const std::uint32_t update_rate = std::max(
        static_cast<std::uint32_t>(MEM_W(0x30, context->r29)), 1U);
    const auto action = g_title_intro_tail_gate.update(
        cinematic_complete, first_title_demo, title_revealed, update_rate);
    if (action == dkr::runtime::intro::TailAction::Hold) {
        context->r2 = 0;
        // sp28 is the title-demo timer completion path. Suppress it together
        // with the cinematic completion signal while the tail is active.
        MEM_W(0x28, context->r29) = 0;
    } else if (action == dkr::runtime::intro::TailAction::Release) {
        // Replay the latched edge exactly once. menu_title_screen_loop then
        // follows its unmodified transition path at 0x80083B9C.
        context->r2 = 1;
    }
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

extern "C" void dkr_postrace_background_stretch_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_postrace_background_stretch_active = false;
    if (!dkr::runtime::enhancements::modern_presentation_enabled() ||
        ExpandedCoverScale() <= 1.0001F) {
        return;
    }

    // bgdraw_texture owns only DKR's repeating post-race mosaic. Scope RT64's
    // rectangle stretch to that function so the texture reaches the host
    // edges while the framed race viewport and every menu/HUD coordinate keep
    // their authored 4:3 placement.
    g_postrace_background_stretch_active = AppendPresentationGroupCommand(
        rdram, context->r4, PresentationGroupMode::BackgroundFillStretch);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_postrace_background_stretch_end(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_postrace_background_stretch_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, context->r4, PresentationGroupMode::World);
    g_postrace_background_stretch_active = false;
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_chequer_background_stretch_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_chequer_background_stretch_active = false;
    if (!dkr::runtime::enhancements::modern_presentation_enabled() ||
        ExpandedCoverScale() <= 1.0001F) {
        return;
    }
    // Battle/challenge results select bgdraw_chequer rather than the normal
    // post-race mosaic. It is the same background-only layer, so give it the
    // same host-width policy without widening the authored replay viewport.
    g_chequer_background_stretch_active = AppendPresentationGroupCommand(
        rdram, context->r4, PresentationGroupMode::BackgroundFillStretch);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_chequer_background_stretch_end(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_chequer_background_stretch_active) {
        return;
    }
    AppendPresentationGroupCommand(rdram, context->r16,
                                   PresentationGroupMode::World);
    g_chequer_background_stretch_active = false;
#else
    (void)rdram;
    (void)context;
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

#else
    (void)rdram;
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
