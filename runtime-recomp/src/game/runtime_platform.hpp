#pragma once

#include "ultramodern/input.hpp"
#include "ultramodern/renderer_context.hpp"

#include <cstddef>
#include <cstdint>

namespace dkr::runtime::platform {

bool initialise();
void shutdown();

#if DKR_RUNTIME_HAS_RT64
ultramodern::renderer::WindowHandle create_window();
void pump_window_events(void*);
void* sdl_window();
void update_ui_gamepad_navigation();
void inject_overlay_toggle_for_test();
#endif

void queue_audio(std::int16_t* samples, std::size_t sample_count);
std::size_t audio_frames_remaining();
void set_audio_frequency(std::uint32_t frequency);
float master_volume();
void set_master_volume(float volume);

void poll_input();
bool get_input(int controller, std::uint16_t* buttons, float* x, float* y);
void set_rumble(int controller, bool enabled);
bool rumble_enabled();
void set_rumble_enabled(bool enabled);
ultramodern::input::connected_device_info_t get_connected_device_info(int controller);

} // namespace dkr::runtime::platform
