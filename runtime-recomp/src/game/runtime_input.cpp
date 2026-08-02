#include "runtime_input.hpp"

#if DKR_RUNTIME_HAS_RT64
#include <SDL.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace {

using dkr::runtime::input::Action;

constexpr int kAxisSourceBase = 1000;
constexpr std::uint16_t kButtonA = 0x8000;
constexpr std::uint16_t kButtonB = 0x4000;
constexpr std::uint16_t kButtonZ = 0x2000;
constexpr std::uint16_t kButtonStart = 0x1000;
constexpr std::uint16_t kDpadUp = 0x0800;
constexpr std::uint16_t kDpadDown = 0x0400;
constexpr std::uint16_t kDpadLeft = 0x0200;
constexpr std::uint16_t kDpadRight = 0x0100;
constexpr std::uint16_t kButtonL = 0x0020;
constexpr std::uint16_t kButtonR = 0x0010;
constexpr std::uint16_t kCUp = 0x0008;
constexpr std::uint16_t kCDown = 0x0004;
constexpr std::uint16_t kCLeft = 0x0002;
constexpr std::uint16_t kCRight = 0x0001;

struct BindingPair {
    int keyboard;
    int controller;
};

#if DKR_RUNTIME_HAS_RT64
constexpr std::array<BindingPair, static_cast<std::size_t>(Action::Count)> kDefaults{{
    {SDL_SCANCODE_W, kAxisSourceBase + SDL_CONTROLLER_AXIS_LEFTY * 2},
    {SDL_SCANCODE_S, kAxisSourceBase + SDL_CONTROLLER_AXIS_LEFTY * 2 + 1},
    {SDL_SCANCODE_A, kAxisSourceBase + SDL_CONTROLLER_AXIS_LEFTX * 2},
    {SDL_SCANCODE_D, kAxisSourceBase + SDL_CONTROLLER_AXIS_LEFTX * 2 + 1},
    {SDL_SCANCODE_SPACE, SDL_CONTROLLER_BUTTON_A},
    {SDL_SCANCODE_LSHIFT, SDL_CONTROLLER_BUTTON_X},
    {SDL_SCANCODE_Z, kAxisSourceBase + SDL_CONTROLLER_AXIS_TRIGGERLEFT * 2 + 1},
    {SDL_SCANCODE_RETURN, SDL_CONTROLLER_BUTTON_START},
    {SDL_SCANCODE_UP, SDL_CONTROLLER_BUTTON_DPAD_UP},
    {SDL_SCANCODE_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    {SDL_SCANCODE_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT},
    {SDL_SCANCODE_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
    {SDL_SCANCODE_Q, SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {SDL_SCANCODE_E, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {SDL_SCANCODE_I, kAxisSourceBase + SDL_CONTROLLER_AXIS_RIGHTY * 2},
    {SDL_SCANCODE_K, kAxisSourceBase + SDL_CONTROLLER_AXIS_RIGHTY * 2 + 1},
    {SDL_SCANCODE_J, kAxisSourceBase + SDL_CONTROLLER_AXIS_RIGHTX * 2},
    {SDL_SCANCODE_L, kAxisSourceBase + SDL_CONTROLLER_AXIS_RIGHTX * 2 + 1},
}};
#else
constexpr std::array<BindingPair, static_cast<std::size_t>(Action::Count)> kDefaults{};
#endif

std::array<BindingPair, static_cast<std::size_t>(Action::Count)> g_bindings = kDefaults;
std::mutex g_binding_mutex;

constexpr std::array<const char*, static_cast<std::size_t>(Action::Count)> kIdentifiers{{
    "stick_up", "stick_down", "stick_left", "stick_right", "a", "b", "z",
    "start", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "l", "r",
    "c_up", "c_down", "c_left", "c_right",
}};

constexpr std::array<const char*, static_cast<std::size_t>(Action::Count)> kLabels{{
    "Analogue up", "Analogue down", "Analogue left", "Analogue right", "A button",
    "B button", "Z trigger", "Start", "D-pad up", "D-pad down", "D-pad left",
    "D-pad right", "L shoulder", "R shoulder", "C-up", "C-down", "C-left", "C-right",
}};

std::size_t Index(Action action) {
    return std::min(static_cast<std::size_t>(action), g_bindings.size() - 1U);
}

#if DKR_RUNTIME_HAS_RT64
float NormaliseAxis(Sint16 value, Sint16 deadzone = 7849) {
    const int magnitude = std::abs(static_cast<int>(value));
    if (magnitude <= deadzone) {
        return 0.0F;
    }
    const float scaled = static_cast<float>(magnitude - deadzone) /
                         static_cast<float>(32767 - deadzone);
    return std::copysign(std::min(scaled, 1.0F), static_cast<float>(value));
}

float SourceValue(SDL_GameController* controller, int source) {
    if (controller == nullptr || source < 0) {
        return 0.0F;
    }
    if (source < SDL_CONTROLLER_BUTTON_MAX) {
        return SDL_GameControllerGetButton(
            controller, static_cast<SDL_GameControllerButton>(source)) != 0 ? 1.0F : 0.0F;
    }
    if (source < kAxisSourceBase) {
        return 0.0F;
    }
    const int encoded = source - kAxisSourceBase;
    const int axis = encoded / 2;
    if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) {
        return 0.0F;
    }
    const bool positive = (encoded & 1) != 0;
    const float value = NormaliseAxis(SDL_GameControllerGetAxis(
        controller, static_cast<SDL_GameControllerAxis>(axis)));
    return positive ? std::max(value, 0.0F) : std::max(-value, 0.0F);
}
#endif

} // namespace

std::size_t dkr::runtime::input::action_count() {
    return static_cast<std::size_t>(Action::Count);
}

const char* dkr::runtime::input::action_identifier(Action action) {
    return kIdentifiers[Index(action)];
}

const char* dkr::runtime::input::action_label(Action action) {
    return kLabels[Index(action)];
}

int dkr::runtime::input::keyboard_binding(Action action) {
    std::scoped_lock lock(g_binding_mutex);
    return g_bindings[Index(action)].keyboard;
}

int dkr::runtime::input::controller_binding(Action action) {
    std::scoped_lock lock(g_binding_mutex);
    return g_bindings[Index(action)].controller;
}

void dkr::runtime::input::set_keyboard_binding(Action action, int scancode) {
    std::scoped_lock lock(g_binding_mutex);
    g_bindings[Index(action)].keyboard = scancode;
}

void dkr::runtime::input::set_controller_binding(Action action, int source) {
    std::scoped_lock lock(g_binding_mutex);
    g_bindings[Index(action)].controller = source;
}

void dkr::runtime::input::reset_defaults() {
    std::scoped_lock lock(g_binding_mutex);
    g_bindings = kDefaults;
}

int dkr::runtime::input::encode_controller_button(int button) {
#if DKR_RUNTIME_HAS_RT64
    return button >= 0 && button < SDL_CONTROLLER_BUTTON_MAX ? button : kUnbound;
#else
    (void)button;
    return kUnbound;
#endif
}

int dkr::runtime::input::encode_controller_axis(int axis, bool positive) {
#if DKR_RUNTIME_HAS_RT64
    return axis >= 0 && axis < SDL_CONTROLLER_AXIS_MAX
        ? kAxisSourceBase + axis * 2 + (positive ? 1 : 0)
        : kUnbound;
#else
    (void)axis;
    (void)positive;
    return kUnbound;
#endif
}

std::string dkr::runtime::input::keyboard_binding_name(int scancode) {
#if DKR_RUNTIME_HAS_RT64
    if (scancode < 0 || scancode >= SDL_NUM_SCANCODES) {
        return "Unbound";
    }
    const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
    return name != nullptr && *name != '\0' ? name : "Unknown key";
#else
    (void)scancode;
    return "Unbound";
#endif
}

std::string dkr::runtime::input::controller_binding_name(int source) {
#if DKR_RUNTIME_HAS_RT64
    if (source >= 0 && source < SDL_CONTROLLER_BUTTON_MAX) {
        const char* name = SDL_GameControllerGetStringForButton(
            static_cast<SDL_GameControllerButton>(source));
        return name != nullptr ? name : "Unknown button";
    }
    if (source >= kAxisSourceBase) {
        const int encoded = source - kAxisSourceBase;
        const int axis = encoded / 2;
        if (axis >= 0 && axis < SDL_CONTROLLER_AXIS_MAX) {
            const char* name = SDL_GameControllerGetStringForAxis(
                static_cast<SDL_GameControllerAxis>(axis));
            return std::string(name != nullptr ? name : "axis") +
                   ((encoded & 1) != 0 ? " +" : " -");
        }
    }
#else
    (void)source;
#endif
    return "Unbound";
}

dkr::runtime::input::State dkr::runtime::input::poll(
    SDL_GameController* controller, bool include_keyboard, bool blocked) {
    State state{};
#if DKR_RUNTIME_HAS_RT64
    if (blocked) {
        return state;
    }
    std::array<BindingPair, static_cast<std::size_t>(Action::Count)> bindings;
    {
        std::scoped_lock lock(g_binding_mutex);
        bindings = g_bindings;
    }
    const Uint8* keys = include_keyboard ? SDL_GetKeyboardState(nullptr) : nullptr;
    const auto value = [&](Action action) {
        const BindingPair& binding = bindings[Index(action)];
        float result = SourceValue(controller, binding.controller);
        if (keys != nullptr && binding.keyboard >= 0 && binding.keyboard < SDL_NUM_SCANCODES &&
            keys[binding.keyboard] != 0) {
            result = 1.0F;
        }
        return result;
    };
    const auto press = [&](Action action, std::uint16_t mask) {
        if (value(action) > 0.5F) {
            state.buttons |= mask;
        }
    };
    press(Action::A, kButtonA);
    press(Action::B, kButtonB);
    press(Action::Z, kButtonZ);
    press(Action::Start, kButtonStart);
    press(Action::DpadUp, kDpadUp);
    press(Action::DpadDown, kDpadDown);
    press(Action::DpadLeft, kDpadLeft);
    press(Action::DpadRight, kDpadRight);
    press(Action::L, kButtonL);
    press(Action::R, kButtonR);
    press(Action::CUp, kCUp);
    press(Action::CDown, kCDown);
    press(Action::CLeft, kCLeft);
    press(Action::CRight, kCRight);
    state.stick_x = std::clamp(value(Action::StickRight) - value(Action::StickLeft), -1.0F, 1.0F);
    state.stick_y = std::clamp(value(Action::StickUp) - value(Action::StickDown), -1.0F, 1.0F);
#else
    (void)controller;
    (void)include_keyboard;
    (void)blocked;
#endif
    return state;
}
