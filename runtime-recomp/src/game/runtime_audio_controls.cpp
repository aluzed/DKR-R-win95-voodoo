#include "runtime_audio_controls.hpp"
#include "audio_mix_policy.hpp"
#include "runtime_enhancements.hpp"

#include "recomp.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace {

std::atomic<float> g_music_volume{1.0F};
std::atomic<float> g_sound_effects_volume{1.0F};
std::atomic<float> g_vehicle_volume{1.0F};
thread_local int g_vehicle_audio_scope_depth = 0;

} // namespace

float dkr::runtime::audio::music_volume() {
    return g_music_volume.load(std::memory_order_acquire);
}

void dkr::runtime::audio::set_music_volume(float volume) {
    g_music_volume.store(clamp_mix_volume(volume), std::memory_order_release);
}

float dkr::runtime::audio::sound_effects_volume() {
    return g_sound_effects_volume.load(std::memory_order_acquire);
}

void dkr::runtime::audio::set_sound_effects_volume(float volume) {
    g_sound_effects_volume.store(clamp_mix_volume(volume), std::memory_order_release);
}

float dkr::runtime::audio::vehicle_volume() {
    return g_vehicle_volume.load(std::memory_order_acquire);
}

void dkr::runtime::audio::set_vehicle_volume(float volume) {
    g_vehicle_volume.store(clamp_mix_volume(volume), std::memory_order_release);
}

extern "C" void dkr_scale_music_volume(std::uint8_t*, recomp_context* context) {
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    const std::uint32_t authored = static_cast<std::uint32_t>(context->r4) & 0xFFU;
    context->r4 = dkr::runtime::audio::scale_authored_volume(
        authored, dkr::runtime::audio::music_volume());
}

extern "C" void dkr_enter_vehicle_audio_scope(std::uint8_t*, recomp_context*) {
    ++g_vehicle_audio_scope_depth;
}

extern "C" void dkr_leave_vehicle_audio_scope(std::uint8_t*, recomp_context*) {
    g_vehicle_audio_scope_depth = std::max(g_vehicle_audio_scope_depth - 1, 0);
}

extern "C" void dkr_scale_sound_effect_volume(std::uint8_t*,
                                                recomp_context* context) {
    constexpr std::uint32_t kVolumeEvent = 1U << 3U;
    if (!dkr::runtime::enhancements::modern_presentation_enabled() ||
        (static_cast<std::uint32_t>(context->r5) & 0xFFFFU) != kVolumeEvent) {
        return;
    }
    std::uint32_t value = static_cast<std::uint32_t>(context->r6);
    value = dkr::runtime::audio::scale_authored_volume(
        value, dkr::runtime::audio::sound_effects_volume());
    if (g_vehicle_audio_scope_depth > 0) {
        value = dkr::runtime::audio::scale_authored_volume(
            value, dkr::runtime::audio::vehicle_volume());
    }
    context->r6 = value;
}
