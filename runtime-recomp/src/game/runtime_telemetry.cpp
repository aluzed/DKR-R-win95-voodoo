#include "runtime_telemetry.hpp"

#include "recomp.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using Clock = std::chrono::steady_clock;
using Fallback = dkr::runtime::telemetry::InterpolationFallback;

struct Counters {
    std::atomic<std::uint64_t> simulation_ticks{0};
    std::atomic<std::uint64_t> input_polls{0};
    std::atomic<std::uint64_t> audio_buffers{0};
    std::atomic<std::uint64_t> audio_frames{0};
    std::atomic<std::uint64_t> graphics_tasks{0};
    std::atomic<std::uint64_t> vi_presents{0};
    std::atomic<std::uint64_t> interpolated_presents{0};
    std::array<std::atomic<std::uint64_t>,
               static_cast<std::size_t>(Fallback::Count)> fallbacks{};
};

struct Snapshot {
    std::uint64_t simulation_ticks = 0;
    std::uint64_t input_polls = 0;
    std::uint64_t audio_buffers = 0;
    std::uint64_t audio_frames = 0;
    std::uint64_t graphics_tasks = 0;
    std::uint64_t vi_presents = 0;
    std::uint64_t interpolated_presents = 0;
    std::array<std::uint64_t, static_cast<std::size_t>(Fallback::Count)>
        fallbacks{};
};

Counters g_counters;

bool EnvironmentEnabled() {
    const char* value = std::getenv("DKR_TIMING_TELEMETRY");
    return value != nullptr && value[0] != '\0' &&
           std::strcmp(value, "0") != 0;
}

Snapshot ReadSnapshot() {
    Snapshot result;
    result.simulation_ticks =
        g_counters.simulation_ticks.load(std::memory_order_relaxed);
    result.input_polls = g_counters.input_polls.load(std::memory_order_relaxed);
    result.audio_buffers =
        g_counters.audio_buffers.load(std::memory_order_relaxed);
    result.audio_frames =
        g_counters.audio_frames.load(std::memory_order_relaxed);
    result.graphics_tasks =
        g_counters.graphics_tasks.load(std::memory_order_relaxed);
    result.vi_presents =
        g_counters.vi_presents.load(std::memory_order_relaxed);
    result.interpolated_presents =
        g_counters.interpolated_presents.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < result.fallbacks.size(); ++i) {
        result.fallbacks[i] =
            g_counters.fallbacks[i].load(std::memory_order_relaxed);
    }
    return result;
}

std::uint64_t Delta(std::uint64_t current, std::uint64_t previous) {
    return current >= previous ? current - previous : 0;
}

double Rate(std::uint64_t count, double seconds) {
    return seconds > 0.0 ? static_cast<double>(count) / seconds : 0.0;
}

} // namespace

bool dkr::runtime::telemetry::enabled() {
    static const bool telemetry_enabled = EnvironmentEnabled();
    return telemetry_enabled;
}

void dkr::runtime::telemetry::record_simulation_tick() {
    if (enabled()) {
        g_counters.simulation_ticks.fetch_add(1, std::memory_order_relaxed);
    }
}

void dkr::runtime::telemetry::record_input_poll() {
    if (enabled()) {
        g_counters.input_polls.fetch_add(1, std::memory_order_relaxed);
    }
}

void dkr::runtime::telemetry::record_audio_buffer(
    std::size_t interleaved_sample_count) {
    if (!enabled()) {
        return;
    }
    g_counters.audio_buffers.fetch_add(1, std::memory_order_relaxed);
    g_counters.audio_frames.fetch_add(interleaved_sample_count / 2U,
                                      std::memory_order_relaxed);
}

void dkr::runtime::telemetry::record_graphics_task() {
    if (enabled()) {
        g_counters.graphics_tasks.fetch_add(1, std::memory_order_relaxed);
    }
}

void dkr::runtime::telemetry::record_vi_present() {
    if (enabled()) {
        g_counters.vi_presents.fetch_add(1, std::memory_order_relaxed);
    }
}

void dkr::runtime::telemetry::record_interpolated_present() {
    record_interpolated_presents(1U);
}

void dkr::runtime::telemetry::record_interpolated_presents(
    std::uint64_t count) {
    if (enabled()) {
        g_counters.interpolated_presents.fetch_add(count,
                                                   std::memory_order_relaxed);
    }
}

void dkr::runtime::telemetry::record_interpolation_fallback(
    InterpolationFallback reason) {
    if (!enabled()) {
        return;
    }
    const auto index = static_cast<std::size_t>(reason);
    if (index < g_counters.fallbacks.size()) {
        g_counters.fallbacks[index].fetch_add(1, std::memory_order_relaxed);
    }
}

void dkr::runtime::telemetry::report_if_due() {
    if (!enabled()) {
        return;
    }

    static auto previous_time = Clock::now();
    static Snapshot previous = ReadSnapshot();
    const auto now = Clock::now();
    const double elapsed = std::chrono::duration<double>(now - previous_time).count();
    if (elapsed < 5.0) {
        return;
    }

    const Snapshot current = ReadSnapshot();
    const auto sim = Delta(current.simulation_ticks, previous.simulation_ticks);
    const auto input = Delta(current.input_polls, previous.input_polls);
    const auto audio_buffers = Delta(current.audio_buffers, previous.audio_buffers);
    const auto audio_frames = Delta(current.audio_frames, previous.audio_frames);
    const auto graphics = Delta(current.graphics_tasks, previous.graphics_tasks);
    const auto vi = Delta(current.vi_presents, previous.vi_presents);
    const auto interpolated =
        Delta(current.interpolated_presents, previous.interpolated_presents);
    std::array<std::uint64_t, static_cast<std::size_t>(Fallback::Count)>
        fallbacks{};
    for (std::size_t i = 0; i < fallbacks.size(); ++i) {
        fallbacks[i] = Delta(current.fallbacks[i], previous.fallbacks[i]);
    }

    std::fprintf(
        stderr,
        "[timing] window=%.3fs sim=%.2fHz input=%.2fHz audio=%.0fframes/s "
        "audio-buffers=%llu gfx=%.2fHz vi=%.2fHz interpolated=%.2fHz "
        "fallbacks={scene:%llu,history:%llu,jump:%llu,queue:%llu}\n",
        elapsed, Rate(sim, elapsed), Rate(input, elapsed),
        Rate(audio_frames, elapsed),
        static_cast<unsigned long long>(audio_buffers), Rate(graphics, elapsed),
        Rate(vi, elapsed), Rate(interpolated, elapsed),
        static_cast<unsigned long long>(fallbacks[0]),
        static_cast<unsigned long long>(fallbacks[1]),
        static_cast<unsigned long long>(fallbacks[2]),
        static_cast<unsigned long long>(fallbacks[3]));

    previous = current;
    previous_time = now;
}

extern "C" void dkr_telemetry_simulation_tick(std::uint8_t*, recomp_context*) {
    dkr::runtime::telemetry::record_simulation_tick();
}
