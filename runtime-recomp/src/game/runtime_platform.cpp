#include "runtime_platform.hpp"
#include "runtime_input.hpp"
#include "ultramodern/ultramodern.hpp"

#if DKR_RUNTIME_HAS_RT64
#include "runtime_ui.hpp"
#include "imgui/imgui.h"
#include <SDL.h>
#if defined(_WIN32)
#include <SDL_syswm.h>
#endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace {

std::atomic<std::uint64_t> g_audio_buffers{0};
constexpr std::size_t kControllerCount = 4;
std::array<std::atomic<std::uint16_t>, kControllerCount> g_buttons{};
std::array<std::atomic<float>, kControllerCount> g_stick_x{};
std::array<std::atomic<float>, kControllerCount> g_stick_y{};
std::atomic<float> g_master_volume{1.0F};
std::atomic<bool> g_rumble_enabled{true};
bool g_menu_test_enabled = false;
int g_last_menu_test_pulse = -1;
std::uint64_t g_menu_test_poll_count = 0;
#if DKR_RUNTIME_HAS_RT64
std::uint64_t g_menu_test_start_ms = 0;
std::uint64_t g_menu_test_warmup_ms = 10'000U;
#endif

#if DKR_RUNTIME_HAS_RT64
std::mutex g_platform_mutex;
SDL_AudioDeviceID g_audio_device = 0;
std::array<SDL_GameController*, kControllerCount> g_controllers{};
SDL_Window* g_window = nullptr;
std::uint32_t g_audio_frequency = 0;
std::vector<std::int16_t> g_audio_swap_buffer;

float NormaliseAxis(Sint16 value, Sint16 deadzone = 7849) {
    const int magnitude = std::abs(static_cast<int>(value));
    if (magnitude <= deadzone) {
        return 0.0F;
    }
    const float scaled = static_cast<float>(magnitude - deadzone) /
                         static_cast<float>(32767 - deadzone);
    return std::copysign(std::min(scaled, 1.0F), static_cast<float>(value));
}

void RefreshControllers() {
    for (SDL_GameController*& controller : g_controllers) {
        if (controller != nullptr &&
            SDL_GameControllerGetAttached(controller) != SDL_TRUE) {
            SDL_GameControllerClose(controller);
            controller = nullptr;
        }
    }
    for (int index = 0; index < SDL_NumJoysticks(); ++index) {
        if (SDL_IsGameController(index) != SDL_TRUE) {
            continue;
        }
        const SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(index);
        const bool already_open = std::any_of(
            g_controllers.begin(), g_controllers.end(), [instance](SDL_GameController* controller) {
                return controller != nullptr && SDL_JoystickInstanceID(
                    SDL_GameControllerGetJoystick(controller)) == instance;
            });
        if (already_open) {
            continue;
        }
        const auto empty = std::find(g_controllers.begin(), g_controllers.end(), nullptr);
        if (empty == g_controllers.end()) {
            break;
        }
        *empty = SDL_GameControllerOpen(index);
        if (*empty != nullptr) {
            const auto player = static_cast<std::size_t>(empty - g_controllers.begin());
            std::fprintf(stderr, "[boot][input] player=%zu controller=%s\n", player + 1U,
                         SDL_GameControllerName(*empty));
        }
    }
}
#endif

} // namespace

bool dkr::runtime::platform::initialise() {
#if DKR_RUNTIME_HAS_RT64
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0) {
        std::fprintf(stderr, "[boot][platform] SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    std::scoped_lock lock(g_platform_mutex);
    RefreshControllers();
#endif
    g_menu_test_enabled = std::getenv("DKR_MENU_SMOKE_TEST") != nullptr;
    g_last_menu_test_pulse = -1;
    g_menu_test_poll_count = 0;
#if DKR_RUNTIME_HAS_RT64
    g_menu_test_start_ms = SDL_GetTicks64();
    g_menu_test_warmup_ms = 10'000U;
    if (const char* warmup = std::getenv("DKR_MENU_SMOKE_WARMUP_MS")) {
        char* end = nullptr;
        const auto parsed = std::strtoull(warmup, &end, 10);
        if (end != warmup && *end == '\0') {
            g_menu_test_warmup_ms = parsed;
        }
    }
#endif
    std::fprintf(stderr,
                 "[boot][input] keyboard: WASD=stick arrows=d-pad Space=A Shift=B "
                 "Z=Z Enter=Start IJKL=C Q=L E=R\n");
    return true;
}

void dkr::runtime::platform::shutdown() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device != 0) {
        SDL_ClearQueuedAudio(g_audio_device);
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
    for (SDL_GameController*& controller : g_controllers) {
        if (controller != nullptr) {
            SDL_GameControllerClose(controller);
            controller = nullptr;
        }
    }
    if (g_window != nullptr) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
#endif
}

#if DKR_RUNTIME_HAS_RT64
ultramodern::renderer::WindowHandle dkr::runtime::platform::create_window() {
    if (g_window == nullptr) {
        Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
        int initial_width = 1440;
        int initial_height = 900;
        // Opt-in deterministic sizing for launcher/layout smoke tests. Normal
        // players receive the roomy 1440x900 presentation above.
        if (const char* width = std::getenv("DKR_TEST_WINDOW_WIDTH")) {
            char* end = nullptr;
            const long parsed = std::strtol(width, &end, 10);
            if (end != width && *end == '\0') {
                initial_width = std::clamp(static_cast<int>(parsed), 800, 7680);
            }
        }
        if (const char* height = std::getenv("DKR_TEST_WINDOW_HEIGHT")) {
            char* end = nullptr;
            const long parsed = std::strtol(height, &end, 10);
            if (end != height && *end == '\0') {
                initial_height = std::clamp(static_cast<int>(parsed), 600, 4320);
            }
        }
        // Keep automated renderer/menu stress runs off the user's desktop. The
        // native renderer still receives a real window and graphics context;
        // only its initial visibility changes, and normal launches never set
        // this opt-in test variable.
        if (std::getenv("DKR_HIDDEN_SMOKE_TEST") != nullptr) {
            flags |= SDL_WINDOW_HIDDEN;
        }
#if defined(__linux__)
        flags |= SDL_WINDOW_VULKAN;
#endif
        g_window = SDL_CreateWindow("DKR Port", SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    initial_width, initial_height, flags);
        if (g_window == nullptr) {
            std::fprintf(stderr, "[boot][window] SDL creation failed: %s\n", SDL_GetError());
        } else {
            // Both the launcher and the transparent in-game overlay are fully
            // responsive down to this size. Prevent a window-manager resize
            // from making controller targets or binding cards unusably small.
            SDL_SetWindowMinimumSize(g_window, 800, 600);
        }
    }

#if defined(_WIN32)
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (g_window == nullptr || SDL_GetWindowWMInfo(g_window, &info) != SDL_TRUE) {
        std::fprintf(stderr, "[boot][window] native handle failed: %s\n", SDL_GetError());
        return {};
    }
    return {.window = info.info.win.window, .thread_id = GetCurrentThreadId()};
#else
    return g_window;
#endif
}

ultramodern::renderer::WindowHandle dkr::runtime::platform::prepare_window_for_game() {
#if defined(__linux__)
    if (g_window != nullptr) {
        const Uint32 existing_flags = SDL_GetWindowFlags(g_window);
        std::fprintf(stderr,
                     "[boot][window] launcher handoff flags=0x%08X vulkan=%s\n",
                     static_cast<unsigned>(existing_flags),
                     (existing_flags & SDL_WINDOW_VULKAN) != 0 ? "yes" : "no");
        if ((existing_flags & SDL_WINDOW_VULKAN) == 0) {
            int x = SDL_WINDOWPOS_CENTERED;
            int y = SDL_WINDOWPOS_CENTERED;
            int width = 1440;
            int height = 900;
            SDL_GetWindowPosition(g_window, &x, &y);
            SDL_GetWindowSize(g_window, &width, &height);
            const bool was_hidden = (existing_flags & SDL_WINDOW_HIDDEN) != 0;
            SDL_DestroyWindow(g_window);
            g_window = nullptr;

            Uint32 replacement_flags =
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_VULKAN;
            if (was_hidden) {
                replacement_flags |= SDL_WINDOW_HIDDEN;
            }
            g_window = SDL_CreateWindow("DKR Port", x, y, width, height,
                                        replacement_flags);
            if (g_window == nullptr) {
                std::fprintf(stderr,
                             "[boot][window] Vulkan handoff recreation failed: %s\n",
                             SDL_GetError());
                return {};
            }
            SDL_SetWindowMinimumSize(g_window, 800, 600);
            std::fprintf(stderr,
                         "[boot][window] recreated Vulkan-capable game window "
                         "flags=0x%08X\n",
                         static_cast<unsigned>(SDL_GetWindowFlags(g_window)));
        }
    }
#endif
    return create_window();
}

void dkr::runtime::platform::pump_window_events(void*) {
    if (dkr::runtime::ui::consume_exit_request()) {
        ultramodern::quit();
        return;
    }
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
            ultramodern::quit();
            return;
        }

        // Overlay lifecycle commands are host-global. Process them before
        // forwarding the event to ImGui so a focused widget cannot swallow
        // Escape, F1 or Back/View and leave the player trapped in the UI.
        const bool keyboard_toggle = event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
            (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE ||
             event.key.keysym.scancode == SDL_SCANCODE_F1);
        const bool controller_toggle = event.type == SDL_CONTROLLERBUTTONDOWN &&
            event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK;
        if (dkr::runtime::ui::input_capture_active()) {
            dkr::runtime::ui::handle_runtime_event(&event);
            continue;
        }
        if (keyboard_toggle || controller_toggle) {
            dkr::runtime::ui::toggle_overlay();
            continue;
        }

        const bool handled = dkr::runtime::ui::handle_runtime_event(&event);
        if (std::getenv("DKR_INPUT_TRACE") != nullptr &&
            (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP)) {
            std::fprintf(stderr,
                         "[test][input] mouse type=%s button=%u state=%u pos=(%d,%d) handled=%d overlay=%d\n",
                         event.type == SDL_MOUSEBUTTONDOWN ? "down" : "up",
                         event.button.button, event.button.state,
                         event.button.x, event.button.y,
                         handled ? 1 : 0,
                         dkr::runtime::ui::overlay_visible() ? 1 : 0);
        }
    }
    if (dkr::runtime::ui::consume_exit_request()) {
        ultramodern::quit();
    }
}

void* dkr::runtime::platform::sdl_window() {
    return g_window;
}

void dkr::runtime::platform::update_ui_gamepad_navigation() {
    ImGuiIO& io = ImGui::GetIO();
    std::scoped_lock lock(g_platform_mutex);
    SDL_GameControllerUpdate();
    RefreshControllers();

    SDL_GameController* controller = g_controllers[0];
    const bool connected = controller != nullptr;
    if (connected) {
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    } else {
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    }

    const auto button = [&](ImGuiKey key, SDL_GameControllerButton source) {
        io.AddKeyEvent(key, connected &&
            SDL_GameControllerGetButton(controller, source) != 0);
    };
    const auto axis = [&](ImGuiKey negative_key, ImGuiKey positive_key,
                          SDL_GameControllerAxis source, bool invert = false) {
        float value = connected
            ? NormaliseAxis(SDL_GameControllerGetAxis(controller, source))
            : 0.0F;
        if (invert) {
            value = -value;
        }
        io.AddKeyAnalogEvent(negative_key, value < 0.0F, std::max(-value, 0.0F));
        io.AddKeyAnalogEvent(positive_key, value > 0.0F, std::max(value, 0.0F));
    };

    button(ImGuiKey_GamepadStart, SDL_CONTROLLER_BUTTON_START);
    button(ImGuiKey_GamepadBack, SDL_CONTROLLER_BUTTON_BACK);
    button(ImGuiKey_GamepadFaceDown, SDL_CONTROLLER_BUTTON_A);
    button(ImGuiKey_GamepadFaceRight, SDL_CONTROLLER_BUTTON_B);
    button(ImGuiKey_GamepadFaceLeft, SDL_CONTROLLER_BUTTON_X);
    button(ImGuiKey_GamepadFaceUp, SDL_CONTROLLER_BUTTON_Y);
    button(ImGuiKey_GamepadDpadLeft, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    button(ImGuiKey_GamepadDpadRight, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    button(ImGuiKey_GamepadDpadUp, SDL_CONTROLLER_BUTTON_DPAD_UP);
    button(ImGuiKey_GamepadDpadDown, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    button(ImGuiKey_GamepadL1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    button(ImGuiKey_GamepadR1, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    button(ImGuiKey_GamepadL3, SDL_CONTROLLER_BUTTON_LEFTSTICK);
    button(ImGuiKey_GamepadR3, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    axis(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
         SDL_CONTROLLER_AXIS_LEFTX);
    axis(ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown,
         SDL_CONTROLLER_AXIS_LEFTY);
    axis(ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight,
         SDL_CONTROLLER_AXIS_RIGHTX);
    axis(ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown,
         SDL_CONTROLLER_AXIS_RIGHTY);

    const float left_trigger = connected
        ? std::max(static_cast<float>(SDL_GameControllerGetAxis(
              controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT)) / 32767.0F, 0.0F)
        : 0.0F;
    const float right_trigger = connected
        ? std::max(static_cast<float>(SDL_GameControllerGetAxis(
              controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) / 32767.0F, 0.0F)
        : 0.0F;
    io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, left_trigger > 0.10F, left_trigger);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, right_trigger > 0.10F, right_trigger);
}

void dkr::runtime::platform::inject_overlay_toggle_for_test() {
    SDL_Event key_down{};
    key_down.type = SDL_KEYDOWN;
    key_down.key.state = SDL_PRESSED;
    key_down.key.repeat = 0;
    key_down.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    key_down.key.keysym.sym = SDLK_ESCAPE;
    SDL_PushEvent(&key_down);

    SDL_Event key_up = key_down;
    key_up.type = SDL_KEYUP;
    key_up.key.state = SDL_RELEASED;
    SDL_PushEvent(&key_up);
}
#endif

void dkr::runtime::platform::queue_audio(std::int16_t* samples,
                                         std::size_t sample_count) {
    const auto index = ++g_audio_buffers;
    if (index <= 10) {
        std::fprintf(stderr, "[boot][audio] queue=%llu pointer=%p samples=%zu\n",
                     static_cast<unsigned long long>(index), static_cast<void*>(samples),
                     sample_count);
    }
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device != 0 && sample_count != 0) {
        const std::size_t maximum_samples =
            static_cast<std::size_t>(std::max(g_audio_frequency, 48000U)) * 2U;
        if (sample_count > maximum_samples || (sample_count & 1U) != 0) {
            std::fprintf(stderr,
                         "[boot][audio] rejected invalid buffer samples=%zu maximum=%zu\n",
                         sample_count, maximum_samples);
            return;
        }
        g_audio_swap_buffer.resize(sample_count);
        for (std::size_t i = 0; i < sample_count; i += 2) {
            // RDRAM's 32-bit word swap leaves each native stereo pair in R,L
            // order. Restore conventional L,R order before sending it to SDL.
            const float volume = g_master_volume.load(std::memory_order_relaxed);
            g_audio_swap_buffer[i] = static_cast<std::int16_t>(
                std::clamp(std::lround(static_cast<float>(samples[i + 1]) * volume),
                           -32768L, 32767L));
            g_audio_swap_buffer[i + 1] = static_cast<std::int16_t>(
                std::clamp(std::lround(static_cast<float>(samples[i]) * volume),
                           -32768L, 32767L));
        }
        const auto byte_count = static_cast<Uint32>(sample_count * sizeof(std::int16_t));
        if (SDL_QueueAudio(g_audio_device, g_audio_swap_buffer.data(), byte_count) != 0) {
            std::fprintf(stderr, "[boot][audio] queue failed: %s\n", SDL_GetError());
        } else if (index <= 10 || index % 300 == 0) {
            const std::size_t queued_frames = SDL_GetQueuedAudioSize(g_audio_device) /
                                              (2U * sizeof(std::int16_t));
            const std::uint64_t queued_ms = g_audio_frequency == 0
                ? 0U
                : (static_cast<std::uint64_t>(queued_frames) * 1000U) /
                      g_audio_frequency;
            std::fprintf(stderr,
                         "[boot][audio] queued-frames=%zu queued-ms=%llu frequency=%u\n",
                         queued_frames, static_cast<unsigned long long>(queued_ms),
                         g_audio_frequency);
        }
    }
#else
    (void)samples;
#endif
    if (index <= 5 || index % 300 == 0) {
        std::fprintf(stderr, "[boot][audio] buffer=%llu samples=%zu\n",
                     static_cast<unsigned long long>(index), sample_count);
    }
}

float dkr::runtime::platform::master_volume() {
    return g_master_volume.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_master_volume(float volume) {
    g_master_volume.store(std::clamp(volume, 0.0F, 1.0F), std::memory_order_release);
}

std::size_t dkr::runtime::platform::audio_frames_remaining() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device != 0) {
        const std::size_t queued_frames = SDL_GetQueuedAudioSize(g_audio_device) /
                                          (2U * sizeof(std::int16_t));
        // osAiGetLength reports the active N64 DMA, not the sum of both AI
        // slots. DKR uses that distinction when sizing its next audio frame.
        const std::size_t one_dma_frames = std::max(g_audio_frequency / 30U, 1U);
        return std::min(queued_frames, one_dma_frames);
    }
#endif
    return 0;
}

void dkr::runtime::platform::set_audio_frequency(std::uint32_t frequency) {
    if (frequency == 0) {
        return;
    }
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device != 0 && g_audio_frequency == frequency) {
        return;
    }
    if (g_audio_device != 0) {
        SDL_ClearQueuedAudio(g_audio_device);
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }

    SDL_AudioSpec desired{};
    SDL_AudioSpec obtained{};
    desired.freq = static_cast<int>(frequency);
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (g_audio_device == 0) {
        std::fprintf(stderr, "[boot][audio] open failed at %u Hz: %s\n",
                     frequency, SDL_GetError());
        g_audio_frequency = 0;
        return;
    }
    g_audio_frequency = static_cast<std::uint32_t>(obtained.freq);
    SDL_PauseAudioDevice(g_audio_device, 0);
    std::fprintf(stderr, "[boot][audio] device opened requested=%u actual=%d format=%04X channels=%u\n",
                 frequency, obtained.freq, obtained.format, obtained.channels);
#else
    std::fprintf(stderr, "[boot][audio] frequency=%u (diagnostic backend)\n", frequency);
#endif
}

void dkr::runtime::platform::poll_input() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    SDL_GameControllerUpdate();
    RefreshControllers();
    const bool blocked = dkr::runtime::ui::overlay_visible();
    for (std::size_t player = 0; player < kControllerCount; ++player) {
        const auto state = dkr::runtime::input::poll(
            g_controllers[player], player == 0U, blocked);
        auto tested_state = state;
        if (player == 0U && g_menu_test_enabled && !blocked) {
            // Skip cinematics and advance the default menu path using brief,
            // separated N64 Start/A edges. Let the boot logos establish their
            // normal state first; injecting immediately can land between two
            // non-interactive cinematic states and make the smoke path depend
            // on host frame rate.
            if (SDL_GetTicks64() - g_menu_test_start_ms >= g_menu_test_warmup_ms) {
                const std::uint64_t poll = g_menu_test_poll_count++;
                const int pulse = static_cast<int>(poll / 12U);
                const bool pulse_down = (poll % 12U) < 2U;
                if (pulse < 48 && pulse_down) {
                    const bool use_start = (pulse % 4) == 0;
                    tested_state.buttons |= use_start ? 0x1000U : 0x8000U;
                    if (pulse != g_last_menu_test_pulse) {
                        g_last_menu_test_pulse = pulse;
                        std::fprintf(stderr, "[test][menu] pulse=%d button=%s\n",
                                     pulse, use_start ? "Start" : "A");
                    }
                }
            }
        }
        const std::uint16_t previous_buttons =
            g_buttons[player].exchange(tested_state.buttons, std::memory_order_acq_rel);
        if (player == 0U && previous_buttons != tested_state.buttons) {
            std::fprintf(stderr, "[boot][input] buttons=%04X stick=(%.2f,%.2f)\n",
                         tested_state.buttons, tested_state.stick_x, tested_state.stick_y);
        }
        g_stick_x[player].store(tested_state.stick_x, std::memory_order_release);
        g_stick_y[player].store(tested_state.stick_y, std::memory_order_release);
    }
#else
    for (std::size_t player = 0; player < kControllerCount; ++player) {
        g_buttons[player].store(0, std::memory_order_release);
        g_stick_x[player].store(0.0F, std::memory_order_release);
        g_stick_y[player].store(0.0F, std::memory_order_release);
    }
#endif
}

bool dkr::runtime::platform::get_input(int controller, std::uint16_t* buttons,
                                       float* x, float* y) {
    if (controller < 0 || controller >= static_cast<int>(kControllerCount)) {
        return false;
    }
    const auto index = static_cast<std::size_t>(controller);
    *buttons = g_buttons[index].load(std::memory_order_acquire);
    *x = g_stick_x[index].load(std::memory_order_acquire);
    *y = g_stick_y[index].load(std::memory_order_acquire);
    return true;
}

void dkr::runtime::platform::set_rumble(int controller, bool enabled) {
#if DKR_RUNTIME_HAS_RT64
    if (controller < 0 || controller >= static_cast<int>(kControllerCount)) {
        return;
    }
    std::scoped_lock lock(g_platform_mutex);
    SDL_GameController* game_controller = g_controllers[static_cast<std::size_t>(controller)];
    if (game_controller != nullptr) {
        const bool active = enabled && g_rumble_enabled.load(std::memory_order_acquire);
        const Uint16 strength = active ? 0xFFFFU : 0U;
        SDL_GameControllerRumble(game_controller, strength, strength,
                                 active ? SDL_HAPTIC_INFINITY : 0U);
    }
#else
    (void)controller;
    (void)enabled;
#endif
}

bool dkr::runtime::platform::rumble_enabled() {
    return g_rumble_enabled.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_rumble_enabled(bool enabled) {
    g_rumble_enabled.store(enabled, std::memory_order_release);
#if DKR_RUNTIME_HAS_RT64
    if (!enabled) {
        std::scoped_lock lock(g_platform_mutex);
        for (SDL_GameController* controller : g_controllers) {
            if (controller != nullptr) {
                SDL_GameControllerRumble(controller, 0U, 0U, 0U);
            }
        }
    }
#endif
}

ultramodern::input::connected_device_info_t
dkr::runtime::platform::get_connected_device_info(int controller) {
    if (controller < 0 || controller >= static_cast<int>(kControllerCount)) {
        return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
    }
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    const bool has_gamepad = g_controllers[static_cast<std::size_t>(controller)] != nullptr;
    if (controller == 0) {
        return {ultramodern::input::Device::Controller,
                has_gamepad ? ultramodern::input::Pak::RumblePak
                            : ultramodern::input::Pak::None};
    }
    return {has_gamepad ? ultramodern::input::Device::Controller
                        : ultramodern::input::Device::None,
            has_gamepad ? ultramodern::input::Pak::RumblePak
                        : ultramodern::input::Pak::None};
#else
    return {ultramodern::input::Device::Controller, ultramodern::input::Pak::None};
#endif
}
