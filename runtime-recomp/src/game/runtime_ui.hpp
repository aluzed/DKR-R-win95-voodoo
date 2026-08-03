#pragma once

#include <filesystem>

struct SDL_Window;
typedef union SDL_Event SDL_Event;

namespace RT64 {
struct Application;
}

namespace dkr::runtime::ui {

struct StartupResult {
    bool start_game = false;
    std::filesystem::path rom_path;
};

void configure(const std::filesystem::path& config_directory);
// Called only after an explicitly selected renderer backend fails and RT64
// successfully recovers with Automatic. Persist the recovered choice so the
// next launch does not repeat the same failure loop.
void persist_graphics_api_fallback();
StartupResult run_startup_screen(SDL_Window* window);

void attach(RT64::Application& application);
void detach(RT64::Application& application);
void draw(RT64::Application& application);
bool handle_runtime_event(SDL_Event* event);
bool input_capture_active();
void toggle_overlay();
bool overlay_visible();
bool consume_exit_request();

} // namespace dkr::runtime::ui
