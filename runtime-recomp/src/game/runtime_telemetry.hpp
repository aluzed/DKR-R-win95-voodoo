#pragma once

#include <cstddef>
#include <cstdint>

namespace dkr::runtime::telemetry {

enum class InterpolationFallback : std::uint8_t {
    SceneUnsupported = 0,
    MissingHistory,
    Discontinuity,
    QueuePressure,
    Count,
};

bool enabled();
void record_simulation_tick();
void record_input_poll();
void record_audio_buffer(std::size_t interleaved_sample_count);
void record_graphics_task();
void record_vi_present();
void record_interpolated_present();
void record_interpolated_presents(std::uint64_t count);
void record_interpolation_fallback(InterpolationFallback reason);

// Called from the presentation thread. Emits one aggregate record every five
// seconds when DKR_TIMING_TELEMETRY is enabled; it never logs per-frame data.
void report_if_due();

} // namespace dkr::runtime::telemetry
