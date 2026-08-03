#include "runtime_enhancements.hpp"

#include "character_select_animation_policy.hpp"
#include "modern_camera_policy.hpp"
#include "runtime_platform.hpp"

#include "recomp.h"

#include <SDL.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>

namespace {

std::atomic<bool> g_maximum_detail_enabled{false};
std::atomic<dkr::runtime::enhancements::PresentationProfile> g_presentation_profile{
    dkr::runtime::enhancements::PresentationProfile::Accurate};
std::atomic<int> g_fov_offset{0};
std::atomic<int> g_view_distance_multiplier{2};
std::atomic<bool> g_extended_culling_enabled{true};
std::atomic<int> g_frustum_guard_percent{5};
std::atomic<bool> g_fit_to_window_enabled{false};

constexpr std::uint32_t kBlockMusicChangeAddress = 0x800DC648U;
constexpr std::uint32_t kDynamicMusicChannelMaskAddress = 0x80115F7CU;
constexpr std::uint32_t kMenuCurrentCharacterAddress = 0x801263C0U;
constexpr std::uint32_t kMusicTempoAddress = 0x80115D30U;
constexpr std::uint16_t kTimeTrialGhostBehaviour = 0x003AU;
constexpr std::uint32_t kFrustumReferenceAddress = 0x800DC8ACU;
constexpr std::uint32_t kViewportLayoutAddress = 0x80120CE0U;
constexpr std::array<std::uint32_t, 4> kSideReferenceOffsets = {
    48U, 60U, 84U, 96U,
};
constexpr std::uint8_t kCharacterChannels[10][2] = {
    {0x0F, 0x64}, {0x0C, 0x07}, {0x09, 0x64}, {0x0A, 0x64}, {0x08, 0x64},
    {0x0B, 0x64}, {0x0D, 0x64}, {0x0E, 0x64}, {0x05, 0x64}, {0x04, 0x64},
};

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

float ReadRdramFloat(std::uint8_t* rdram, std::uint32_t address) {
    return std::bit_cast<float>(static_cast<std::uint32_t>(
        MEM_W(0, RdramAddress(address))));
}

void WriteRdramFloat(std::uint8_t* rdram, std::uint32_t address, float value) {
    MEM_W(0, RdramAddress(address)) = std::bit_cast<std::uint32_t>(value);
}

struct FrustumReferenceScope {
    std::array<float, kSideReferenceOffsets.size()> saved{};
    bool active = false;
};

thread_local FrustumReferenceScope g_frustum_scope;
std::atomic<bool> g_logged_extended_frustum{false};
std::atomic<int> g_last_logged_fov{-1};
float g_character_select_animation_phase = 0.0F;
bool g_character_select_animation_active = false;

} // namespace

bool dkr::runtime::enhancements::maximum_detail_requested() {
    return g_maximum_detail_enabled.load(std::memory_order_acquire);
}

bool dkr::runtime::enhancements::maximum_detail_enabled() {
    return maximum_detail_effective(presentation_profile(),
                                    maximum_detail_requested());
}

void dkr::runtime::enhancements::set_maximum_detail_enabled(bool enabled) {
    g_maximum_detail_enabled.store(enabled, std::memory_order_release);
}

dkr::runtime::enhancements::PresentationProfile
dkr::runtime::enhancements::presentation_profile() {
    return g_presentation_profile.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_presentation_profile(
    PresentationProfile profile) {
    g_presentation_profile.store(normalise_presentation_profile(profile),
                                 std::memory_order_release);
}

bool dkr::runtime::enhancements::modern_presentation_enabled() {
    return presentation_profile() == PresentationProfile::Modern;
}

int dkr::runtime::enhancements::fov_offset() {
    return g_fov_offset.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_fov_offset(int offset) {
    g_fov_offset.store(clamp_fov_offset(offset), std::memory_order_release);
}

int dkr::runtime::enhancements::view_distance_multiplier() {
    return g_view_distance_multiplier.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_view_distance_multiplier(int multiplier) {
    g_view_distance_multiplier.store(
        clamp_view_distance_multiplier(multiplier), std::memory_order_release);
}

bool dkr::runtime::enhancements::extended_culling_requested() {
    return g_extended_culling_enabled.load(std::memory_order_acquire);
}

bool dkr::runtime::enhancements::extended_culling_enabled() {
    return modern_presentation_enabled() && extended_culling_requested();
}

void dkr::runtime::enhancements::set_extended_culling_enabled(bool enabled) {
    g_extended_culling_enabled.store(enabled, std::memory_order_release);
}

int dkr::runtime::enhancements::frustum_guard_percent() {
    return g_frustum_guard_percent.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_frustum_guard_percent(int percent) {
    g_frustum_guard_percent.store(
        clamp_frustum_guard_percent(percent), std::memory_order_release);
}

bool dkr::runtime::enhancements::fit_to_window_enabled() {
    return g_fit_to_window_enabled.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_fit_to_window_enabled(bool enabled) {
    g_fit_to_window_enabled.store(enabled, std::memory_order_release);
}

extern "C" void dkr_character_select_music_unblock(std::uint8_t* rdram,
                                                    recomp_context*) {
    // The character-select initializer immediately starts its own sequence and
    // then restores DKR's music-change lock. Clear a stale lock only at that
    // ownership boundary so the intended sequence can replace intro/menu music.
    const std::uint32_t previous = MEM_W(0, RdramAddress(kBlockMusicChangeAddress));
    MEM_W(0, RdramAddress(kBlockMusicChangeAddress)) = 0;
    std::fprintf(stderr, "[boot][audio] character-select music ownership (previous lock=%u)\n",
                 previous);
}

extern "C" void dkr_character_select_music_mask(std::uint8_t* rdram,
                                                 recomp_context*) {
    // music_play queues SEQUENCE_CHOOSE_YOUR_RACER and resets the pending
    // dynamic-channel mask before the original initializer mutes channels on
    // the old sequence player. Seed the queued sequence explicitly so it
    // starts with only the shared backing channels and the selected racer's
    // arrangement. 0x64 is DKR's invalid/no-secondary-channel sentinel.
    const std::uint8_t selected = MEM_BU(0, RdramAddress(kMenuCurrentCharacterAddress));
    std::uint32_t mask = 0xFFFFU;
    mask &= ~(1U << 6U);
    for (const auto& channels : kCharacterChannels) {
        for (const std::uint8_t channel : channels) {
            if (channel < 16U) {
                mask &= ~(1U << channel);
            }
        }
    }
    if (selected < 10U) {
        for (const std::uint8_t channel : kCharacterChannels[selected]) {
            if (channel < 16U) {
                mask |= 1U << channel;
            }
        }
    }
    MEM_W(0, RdramAddress(kDynamicMusicChannelMaskAddress)) = mask;
    // Character-select models are authored to dance to the music beat. Reset
    // the deterministic beat phase at the same sequence ownership boundary;
    // the original audio clock continues running and the mix remains exact.
    g_character_select_animation_phase = 0.0F;
    g_character_select_animation_active = true;
    std::fprintf(stderr,
                 "[boot][audio] character-select pending channel mask=%04X selected=%u\n",
                 static_cast<unsigned>(mask), static_cast<unsigned>(selected));
}

extern "C" void dkr_character_select_animation_tick(std::uint8_t* rdram,
                                                      recomp_context* context) {
    if (!g_character_select_animation_active) {
        return;
    }
    const int tempo = static_cast<std::int16_t>(
        MEM_H(0, RdramAddress(kMusicTempoAddress)));
    g_character_select_animation_phase =
        dkr::runtime::enhancements::advance_character_select_phase(
            g_character_select_animation_phase,
            static_cast<std::int32_t>(context->r4), tempo);
}

extern "C" void dkr_character_select_animation_fraction(std::uint8_t*,
                                                          recomp_context* context) {
    if (g_character_select_animation_active) {
        context->f0.fl = g_character_select_animation_phase;
    }
}

extern "C" void dkr_apply_maximum_racer_detail(std::uint8_t* rdram,
                                                recomp_context* context) {
    if (!dkr::runtime::enhancements::maximum_detail_enabled()) {
        return;
    }

    // set_temp_model_transforms has already found the first/last valid model
    // and clamped its chosen index when this hook runs. r4 is the first valid
    // (highest-detail) index, r8 is the index about to be stored, and r16 is
    // the Object pointer. Preserve the ghost's deliberately distinct model.
    const std::uint16_t behaviour = MEM_HU(0x48, context->r16);
    if (behaviour != kTimeTrialGhostBehaviour) {
        context->r8 = context->r4;
    }
}

extern "C" void dkr_apply_gameplay_fov(std::uint8_t*,
                                        recomp_context* context) {
    const int authored = static_cast<std::int32_t>(context->r12);
    if (authored <= 0 || authored > 120) {
        return;
    }
    const int effective = dkr::runtime::enhancements::effective_gameplay_fov(
        dkr::runtime::enhancements::presentation_profile(), authored,
        dkr::runtime::enhancements::fov_offset());
    context->r12 = static_cast<gpr>(effective);
    if (effective != authored &&
        g_last_logged_fov.exchange(effective, std::memory_order_relaxed) !=
            effective) {
        std::fprintf(stderr,
                     "[boot][modern] gameplay FOV authored=%d effective=%d\n",
                     authored, effective);
    }
}

extern "C" void dkr_extend_object_draw_distance(std::uint8_t*,
                                                  recomp_context* context) {
    const int authored = static_cast<std::int32_t>(context->r3);
    context->r3 = static_cast<gpr>(
        dkr::runtime::enhancements::effective_view_distance(
            dkr::runtime::enhancements::presentation_profile(), authored,
            dkr::runtime::enhancements::view_distance_multiplier()));
}

extern "C" void dkr_extended_frustum_begin(std::uint8_t* rdram,
                                            recomp_context*) {
    // Defensive recovery for an interrupted/nested scope. DKR normally calls
    // this serially once per viewport, but never leave the authored table in a
    // widened state if a host-side exception or future call-site changes that.
    if (g_frustum_scope.active) {
        for (std::size_t index = 0; index < kSideReferenceOffsets.size(); ++index) {
            WriteRdramFloat(rdram,
                            kFrustumReferenceAddress + kSideReferenceOffsets[index],
                            g_frustum_scope.saved[index]);
        }
        g_frustum_scope.active = false;
    }

    auto* window = static_cast<SDL_Window*>(
        dkr::runtime::platform::sdl_window());
    if (window == nullptr) {
        return;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    const int layout = static_cast<std::int32_t>(
        MEM_W(0, RdramAddress(kViewportLayoutAddress)));
    const float scale = dkr::runtime::enhancements::frustum_horizontal_scale(
        dkr::runtime::enhancements::presentation_profile(),
        dkr::runtime::enhancements::fit_to_window_enabled(),
        dkr::runtime::enhancements::extended_culling_enabled(),
        static_cast<float>(width) / static_cast<float>(height), layout,
        dkr::runtime::enhancements::frustum_guard_percent());
    if (scale <= 1.0001F) {
        return;
    }
    for (std::size_t index = 0; index < kSideReferenceOffsets.size(); ++index) {
        const std::uint32_t address =
            kFrustumReferenceAddress + kSideReferenceOffsets[index];
        g_frustum_scope.saved[index] = ReadRdramFloat(rdram, address);
        WriteRdramFloat(rdram, address, g_frustum_scope.saved[index] * scale);
    }
    g_frustum_scope.active = true;
    if (!g_logged_extended_frustum.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr,
                     "[boot][modern] CPU frustum scale=%.3f layout=%d window=%dx%d\n",
                     scale, layout, width, height);
    }
}

extern "C" void dkr_extended_frustum_end(std::uint8_t* rdram,
                                          recomp_context*) {
    if (!g_frustum_scope.active) {
        return;
    }
    for (std::size_t index = 0; index < kSideReferenceOffsets.size(); ++index) {
        WriteRdramFloat(rdram,
                        kFrustumReferenceAddress + kSideReferenceOffsets[index],
                        g_frustum_scope.saved[index]);
    }
    g_frustum_scope.active = false;
}
