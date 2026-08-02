#include "runtime_enhancements.hpp"

#include "recomp.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

std::atomic<bool> g_maximum_detail_enabled{false};
std::atomic<dkr::runtime::enhancements::PresentationProfile> g_presentation_profile{
    dkr::runtime::enhancements::PresentationProfile::Accurate};

constexpr std::uint32_t kBlockMusicChangeAddress = 0x800DC648U;
constexpr std::uint32_t kDynamicMusicChannelMaskAddress = 0x80115F7CU;
constexpr std::uint32_t kMenuCurrentCharacterAddress = 0x801263C0U;
constexpr std::uint16_t kTimeTrialGhostBehaviour = 0x003AU;
constexpr std::uint8_t kCharacterChannels[10][2] = {
    {0x0F, 0x64}, {0x0C, 0x07}, {0x09, 0x64}, {0x0A, 0x64}, {0x08, 0x64},
    {0x0B, 0x64}, {0x0D, 0x64}, {0x0E, 0x64}, {0x05, 0x64}, {0x04, 0x64},
};

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

} // namespace

bool dkr::runtime::enhancements::maximum_detail_enabled() {
    return g_maximum_detail_enabled.load(std::memory_order_acquire);
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
    g_presentation_profile.store(profile, std::memory_order_release);
}

bool dkr::runtime::enhancements::modern_presentation_enabled() {
    return presentation_profile() == PresentationProfile::Modern;
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
    std::fprintf(stderr,
                 "[boot][audio] character-select pending channel mask=%04X selected=%u\n",
                 static_cast<unsigned>(mask), static_cast<unsigned>(selected));
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
