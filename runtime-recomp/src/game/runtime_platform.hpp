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
ultramodern::renderer::WindowHandle prepare_window_for_game();
void pump_window_events(void*);
void* sdl_window();
bool handle_window_shortcut(const void* event, bool renderer_active);
void update_fullscreen_cursor(const void* event = nullptr);
void update_ui_gamepad_navigation();
#endif

void queue_audio(std::int16_t* samples, std::size_t sample_count);
std::size_t audio_frames_remaining();
void set_audio_frequency(std::uint32_t frequency);
float master_volume();
void set_master_volume(float volume);
float bass_gain();
void set_bass_gain(float decibels);
float mid_gain();
void set_mid_gain(float decibels);
float treble_gain();
void set_treble_gain(float decibels);

void poll_input();
bool get_input(int controller, std::uint16_t* buttons, float* x, float* y);
void set_rumble(int controller, bool enabled);
bool rumble_enabled();
void set_rumble_enabled(bool enabled);
float rumble_strength();
void set_rumble_strength(float strength);
bool gyro_available();
ultramodern::input::connected_device_info_t get_connected_device_info(int controller);

} // namespace dkr::runtime::platform
