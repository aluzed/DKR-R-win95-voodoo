#include "runtime_input.hpp"
#include "motion_steering_policy.hpp"
#include "runtime_enhancements.hpp"

#if DKR_RUNTIME_HAS_RT64
#include <SDL.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>

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
#if DKR_RUNTIME_HAS_RT64
dkr::runtime::input::ShortcutBinding g_quick_restart_keyboard{
    SDL_SCANCODE_LCTRL, SDL_SCANCODE_R};
dkr::runtime::input::ShortcutBinding g_quick_restart_controller{
    SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_START};
#else
dkr::runtime::input::ShortcutBinding g_quick_restart_keyboard{};
dkr::runtime::input::ShortcutBinding g_quick_restart_controller{};
#endif
std::mutex g_shortcut_mutex;
std::atomic<bool> g_quick_restart_enabled{false};
std::atomic<bool> g_quick_restart_requested{false};
std::atomic<bool> g_quick_restart_held{false};
std::atomic<bool> g_gyro_enabled{false};
std::atomic<float> g_gyro_sensitivity{100.0F};
std::atomic<float> g_gyro_y_sensitivity{100.0F};
std::atomic<float> g_gyro_deadzone{2.0F};
std::atomic<bool> g_gyro_inverted{false};
std::atomic<bool> g_gyro_y_inverted{false};
std::atomic<dkr::runtime::input::GyroAxis> g_gyro_axis{
    dkr::runtime::input::GyroAxis::Roll};
std::atomic<float> g_gyro_bias{0.0F};
std::atomic<float> g_gyro_y_bias{0.0F};
std::atomic<float> g_gyro_calibration_sum{0.0F};
std::atomic<float> g_gyro_y_calibration_sum{0.0F};
std::atomic<int> g_gyro_calibration_samples{0};
std::atomic<int> g_gyro_calibration_remaining{0};
constexpr int kGyroCalibrationSampleCount = 90;
std::mutex g_gyro_motion_mutex;
float g_gyro_angle_radians = 0.0F;
float g_gyro_y_angle_radians = 0.0F;
std::chrono::steady_clock::time_point g_gyro_last_sample{};
bool g_gyro_has_last_sample = false;
std::atomic<float> g_stick_deadzone{23.95F};
std::atomic<float> g_stick_anti_deadzone{0.0F};
std::atomic<float> g_stick_sensitivity{100.0F};
std::atomic<float> g_stick_curve{1.0F};
std::atomic<bool> g_stick_x_inverted{false};
std::atomic<bool> g_stick_y_inverted{false};
std::atomic<float> g_trigger_threshold{0.5F};

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
    const bool modern = dkr::runtime::enhancements::modern_presentation_enabled();
    const Sint16 deadzone = modern
        ? static_cast<Sint16>(std::lround(
              std::clamp(g_stick_deadzone.load(std::memory_order_relaxed),
                         0.0F, 35.0F) * 32767.0F / 100.0F))
        : 7849;
    const float value = NormaliseAxis(SDL_GameControllerGetAxis(
        controller, static_cast<SDL_GameControllerAxis>(axis)), deadzone);
    return positive ? std::max(value, 0.0F) : std::max(-value, 0.0F);
}

bool KeyboardSourceHeld(const Uint8* keys, int source) {
    return keys != nullptr && source >= 0 && source < SDL_NUM_SCANCODES &&
           keys[source] != 0;
}

bool ControllerSourceHeld(SDL_GameController* controller, int source) {
    return source >= 0 && source < SDL_CONTROLLER_BUTTON_MAX &&
           SourceValue(controller, source) > 0.5F;
}

template <typename Predicate>
bool ShortcutHeld(const dkr::runtime::input::ShortcutBinding& binding,
                  Predicate&& predicate) {
    if (binding.primary == dkr::runtime::input::kUnbound ||
        !predicate(binding.primary)) {
        return false;
    }
    return binding.secondary == dkr::runtime::input::kUnbound ||
           predicate(binding.secondary);
}

bool ShortcutContains(const dkr::runtime::input::ShortcutBinding& binding,
                      int source) {
    return source != dkr::runtime::input::kUnbound &&
           (binding.primary == source || binding.secondary == source);
}

float ShapeStick(float value, bool inverted) {
    const float magnitude = std::fabs(value);
    if (magnitude <= 0.0F) {
        return 0.0F;
    }
    const float anti = std::clamp(
        g_stick_anti_deadzone.load(std::memory_order_relaxed) / 100.0F,
        0.0F, 0.5F);
    const float curve = std::clamp(
        g_stick_curve.load(std::memory_order_relaxed), 0.5F, 2.5F);
    const float sensitivity = std::clamp(
        g_stick_sensitivity.load(std::memory_order_relaxed) / 100.0F,
        0.5F, 1.5F);
    float shaped = anti + (1.0F - anti) * std::pow(magnitude, curve);
    shaped = std::clamp(shaped * sensitivity, 0.0F, 1.0F);
    return std::copysign(shaped, inverted ? -value : value);
}

struct GyroSample {
    float x = 0.0F;
    float y = 0.0F;
};

std::optional<GyroSample> PollGyro(SDL_GameController* controller) {
    using namespace dkr::runtime::input;
    if (controller == nullptr || !gyro_enabled() ||
        !dkr::runtime::enhancements::modern_presentation_enabled() ||
        SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) != SDL_TRUE) {
        recenter_gyro();
        return std::nullopt;
    }
    if (SDL_GameControllerIsSensorEnabled(controller, SDL_SENSOR_GYRO) != SDL_TRUE &&
        SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO,
                                           SDL_TRUE) != 0) {
        recenter_gyro();
        return std::nullopt;
    }
    float sensor[3]{};
    if (SDL_GameControllerGetSensorData(controller, SDL_SENSOR_GYRO,
                                        sensor, 3) != 0) {
        recenter_gyro();
        return std::nullopt;
    }
    const float raw_x = gyro_axis() == GyroAxis::Yaw ? sensor[1] : sensor[2];
    const float raw_y = sensor[0];
    const int remaining = g_gyro_calibration_remaining.load(
        std::memory_order_acquire);
    if (remaining > 0) {
        const float sum = g_gyro_calibration_sum.fetch_add(
            raw_x, std::memory_order_acq_rel) + raw_x;
        const float y_sum = g_gyro_y_calibration_sum.fetch_add(
            raw_y, std::memory_order_acq_rel) + raw_y;
        const int samples = g_gyro_calibration_samples.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (g_gyro_calibration_remaining.fetch_sub(
                1, std::memory_order_acq_rel) == 1) {
            g_gyro_bias.store(sum / static_cast<float>(samples),
                              std::memory_order_release);
            g_gyro_y_bias.store(y_sum / static_cast<float>(samples),
                                std::memory_order_release);
            recenter_gyro();
        }
        return GyroSample{};
    }
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock motion_lock(g_gyro_motion_mutex);
    float delta_seconds = 0.0F;
    if (g_gyro_has_last_sample) {
        delta_seconds = std::chrono::duration<float>(
            now - g_gyro_last_sample).count();
    }
    g_gyro_last_sample = now;
    g_gyro_has_last_sample = true;
    g_gyro_angle_radians = integrate_gyro_angle(
        g_gyro_angle_radians, raw_x,
        g_gyro_bias.load(std::memory_order_acquire), gyro_deadzone(),
        delta_seconds, gyro_inverted());
    g_gyro_y_angle_radians = integrate_gyro_angle(
        g_gyro_y_angle_radians, raw_y,
        g_gyro_y_bias.load(std::memory_order_acquire), gyro_deadzone(),
        delta_seconds, gyro_y_inverted());
    return GyroSample{
        gyro_angle_to_steering(g_gyro_angle_radians, gyro_sensitivity()),
        gyro_angle_to_steering(g_gyro_y_angle_radians, gyro_y_sensitivity())};
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
    const std::size_t target = Index(action);
    if (scancode != kUnbound) {
        for (std::size_t index = 0; index < g_bindings.size(); ++index) {
            if (index != target && g_bindings[index].keyboard == scancode) {
                g_bindings[index].keyboard = kUnbound;
            }
        }
    }
    g_bindings[target].keyboard = scancode;
}

void dkr::runtime::input::set_controller_binding(Action action, int source) {
    std::scoped_lock lock(g_binding_mutex);
    const std::size_t target = Index(action);
    if (source != kUnbound) {
        for (std::size_t index = 0; index < g_bindings.size(); ++index) {
            if (index != target && g_bindings[index].controller == source) {
                g_bindings[index].controller = kUnbound;
            }
        }
    }
    g_bindings[target].controller = source;
}

void dkr::runtime::input::reset_defaults() {
    {
        std::scoped_lock lock(g_binding_mutex);
        g_bindings = kDefaults;
    }
#if DKR_RUNTIME_HAS_RT64
    {
        std::scoped_lock lock(g_shortcut_mutex);
        g_quick_restart_keyboard = {SDL_SCANCODE_LCTRL, SDL_SCANCODE_R};
        g_quick_restart_controller = {
            SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_START};
    }
#endif
    g_quick_restart_enabled.store(false, std::memory_order_release);
    g_quick_restart_requested.store(false, std::memory_order_release);
    g_quick_restart_held.store(false, std::memory_order_release);
}

bool dkr::runtime::input::quick_restart_enabled() {
    return g_quick_restart_enabled.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_quick_restart_enabled(bool enabled) {
    g_quick_restart_enabled.store(enabled, std::memory_order_release);
    if (!enabled) {
        g_quick_restart_requested.store(false, std::memory_order_release);
        g_quick_restart_held.store(false, std::memory_order_release);
    }
}

dkr::runtime::input::ShortcutBinding
dkr::runtime::input::quick_restart_keyboard_binding() {
    std::scoped_lock lock(g_shortcut_mutex);
    return g_quick_restart_keyboard;
}

dkr::runtime::input::ShortcutBinding
dkr::runtime::input::quick_restart_controller_binding() {
    std::scoped_lock lock(g_shortcut_mutex);
    return g_quick_restart_controller;
}

void dkr::runtime::input::set_quick_restart_keyboard_binding(
    ShortcutBinding binding) {
    std::scoped_lock lock(g_shortcut_mutex);
    g_quick_restart_keyboard = binding;
}

void dkr::runtime::input::set_quick_restart_controller_binding(
    ShortcutBinding binding) {
    std::scoped_lock lock(g_shortcut_mutex);
    g_quick_restart_controller = binding;
}

bool dkr::runtime::input::consume_quick_restart_request() {
    return g_quick_restart_requested.exchange(false, std::memory_order_acq_rel);
}

float dkr::runtime::input::stick_deadzone() { return g_stick_deadzone.load(); }
void dkr::runtime::input::set_stick_deadzone(float value) {
    g_stick_deadzone.store(std::clamp(value, 0.0F, 35.0F));
}
float dkr::runtime::input::stick_anti_deadzone() { return g_stick_anti_deadzone.load(); }
void dkr::runtime::input::set_stick_anti_deadzone(float value) {
    g_stick_anti_deadzone.store(std::clamp(value, 0.0F, 50.0F));
}
float dkr::runtime::input::stick_sensitivity() { return g_stick_sensitivity.load(); }
void dkr::runtime::input::set_stick_sensitivity(float value) {
    g_stick_sensitivity.store(std::clamp(value, 50.0F, 150.0F));
}
float dkr::runtime::input::stick_curve() { return g_stick_curve.load(); }
void dkr::runtime::input::set_stick_curve(float value) {
    g_stick_curve.store(std::clamp(value, 0.5F, 2.5F));
}
bool dkr::runtime::input::stick_x_inverted() { return g_stick_x_inverted.load(); }
void dkr::runtime::input::set_stick_x_inverted(bool value) { g_stick_x_inverted.store(value); }
bool dkr::runtime::input::stick_y_inverted() { return g_stick_y_inverted.load(); }
void dkr::runtime::input::set_stick_y_inverted(bool value) { g_stick_y_inverted.store(value); }
float dkr::runtime::input::trigger_threshold() { return g_trigger_threshold.load(); }
void dkr::runtime::input::set_trigger_threshold(float value) {
    g_trigger_threshold.store(std::clamp(value, 0.05F, 0.95F));
}

bool dkr::runtime::input::gyro_enabled() {
    return g_gyro_enabled.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_enabled(bool enabled) {
    g_gyro_enabled.store(enabled, std::memory_order_release);
    recenter_gyro();
}

float dkr::runtime::input::gyro_sensitivity() {
    return g_gyro_sensitivity.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_sensitivity(float percent) {
    g_gyro_sensitivity.store(clamp_gyro_sensitivity(percent),
                             std::memory_order_release);
}

float dkr::runtime::input::gyro_y_sensitivity() {
    return g_gyro_y_sensitivity.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_y_sensitivity(float percent) {
    g_gyro_y_sensitivity.store(clamp_gyro_sensitivity(percent),
                               std::memory_order_release);
}

float dkr::runtime::input::gyro_deadzone() {
    return g_gyro_deadzone.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_deadzone(float degrees_per_second) {
    g_gyro_deadzone.store(clamp_gyro_deadzone(degrees_per_second),
                         std::memory_order_release);
}

bool dkr::runtime::input::gyro_inverted() {
    return g_gyro_inverted.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_inverted(bool inverted) {
    g_gyro_inverted.store(inverted, std::memory_order_release);
}

bool dkr::runtime::input::gyro_y_inverted() {
    return g_gyro_y_inverted.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_y_inverted(bool inverted) {
    g_gyro_y_inverted.store(inverted, std::memory_order_release);
}

dkr::runtime::input::GyroAxis dkr::runtime::input::gyro_axis() {
    return g_gyro_axis.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_axis(GyroAxis axis) {
    g_gyro_axis.store(axis == GyroAxis::Yaw ? GyroAxis::Yaw : GyroAxis::Roll,
                      std::memory_order_release);
    recenter_gyro();
}

void dkr::runtime::input::begin_gyro_calibration() {
    g_gyro_calibration_sum.store(0.0F, std::memory_order_release);
    g_gyro_y_calibration_sum.store(0.0F, std::memory_order_release);
    g_gyro_calibration_samples.store(0, std::memory_order_release);
    g_gyro_calibration_remaining.store(kGyroCalibrationSampleCount,
                                       std::memory_order_release);
    recenter_gyro();
}

void dkr::runtime::input::recenter_gyro() {
    std::scoped_lock motion_lock(g_gyro_motion_mutex);
    g_gyro_angle_radians = 0.0F;
    g_gyro_y_angle_radians = 0.0F;
    g_gyro_last_sample = {};
    g_gyro_has_last_sample = false;
}

float dkr::runtime::input::gyro_steering_position() {
    std::scoped_lock motion_lock(g_gyro_motion_mutex);
    return gyro_angle_to_steering(g_gyro_angle_radians,
                                  gyro_sensitivity());
}

float dkr::runtime::input::gyro_steering_y_position() {
    std::scoped_lock motion_lock(g_gyro_motion_mutex);
    return gyro_angle_to_steering(g_gyro_y_angle_radians,
                                  gyro_y_sensitivity());
}

bool dkr::runtime::input::gyro_calibrating() {
    return g_gyro_calibration_remaining.load(std::memory_order_acquire) > 0;
}

float dkr::runtime::input::gyro_calibration_progress() {
    const int remaining = g_gyro_calibration_remaining.load(std::memory_order_acquire);
    return std::clamp(1.0F - static_cast<float>(remaining) /
                                 static_cast<float>(kGyroCalibrationSampleCount),
                      0.0F, 1.0F);
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
    SDL_GameController* controller, SDL_GameController* gyro_controller,
    bool include_keyboard, bool blocked) {
    State state{};
#if DKR_RUNTIME_HAS_RT64
    // Motion steering belongs to Controller 1. Polling the shared gyro
    // accumulator for the empty Controller 2-4 slots used to recenter it three
    // times after every valid sample, leaving every subsequent Player 1 sample
    // with a zero delta and therefore no steering output.
    const std::optional<GyroSample> gyro = owns_gyro_accumulator(include_keyboard)
        ? PollGyro(gyro_controller)
        : std::nullopt;
    std::array<BindingPair, static_cast<std::size_t>(Action::Count)> bindings;
    {
        std::scoped_lock lock(g_binding_mutex);
        bindings = g_bindings;
    }
    const Uint8* keys = include_keyboard ? SDL_GetKeyboardState(nullptr) : nullptr;
    ShortcutBinding quick_keyboard;
    ShortcutBinding quick_controller;
    {
        std::scoped_lock lock(g_shortcut_mutex);
        quick_keyboard = g_quick_restart_keyboard;
        quick_controller = g_quick_restart_controller;
    }
    bool keyboard_shortcut_held = false;
    bool controller_shortcut_held = false;
    if (include_keyboard && !blocked && quick_restart_enabled() &&
        dkr::runtime::enhancements::modern_presentation_enabled()) {
        keyboard_shortcut_held = ShortcutHeld(
            quick_keyboard,
            [&](int source) { return KeyboardSourceHeld(keys, source); });
        controller_shortcut_held = ShortcutHeld(
            quick_controller,
            [&](int source) { return ControllerSourceHeld(controller, source); });
        const bool shortcut_held =
            keyboard_shortcut_held || controller_shortcut_held;
        const bool was_held = g_quick_restart_held.exchange(
            shortcut_held, std::memory_order_acq_rel);
        if (shortcut_held && !was_held) {
            g_quick_restart_requested.store(true, std::memory_order_release);
        }
    } else if (include_keyboard) {
        g_quick_restart_held.store(false, std::memory_order_release);
    }
    if (blocked) {
        // Keep sampling Controller 1 while the overlay is open so calibration
        // and both live preview bars remain truthful. Gameplay receives a
        // neutral sample until the overlay closes.
        return state;
    }
    const auto value = [&](Action action) {
        const BindingPair& binding = bindings[Index(action)];
        float result = controller_shortcut_held &&
                ShortcutContains(quick_controller, binding.controller)
            ? 0.0F
            : SourceValue(controller, binding.controller);
        if (keys != nullptr && binding.keyboard >= 0 && binding.keyboard < SDL_NUM_SCANCODES &&
            keys[binding.keyboard] != 0 &&
            !(keyboard_shortcut_held &&
              ShortcutContains(quick_keyboard, binding.keyboard))) {
            result = 1.0F;
        }
        return result;
    };
    const auto press = [&](Action action, std::uint16_t mask, float threshold = 0.5F) {
        if (value(action) > threshold) {
            state.buttons |= mask;
        }
    };
    press(Action::A, kButtonA);
    press(Action::B, kButtonB);
    press(Action::Z, kButtonZ,
          dkr::runtime::enhancements::modern_presentation_enabled()
              ? trigger_threshold() : 0.5F);
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
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        state.stick_x = ShapeStick(state.stick_x, stick_x_inverted());
        state.stick_y = ShapeStick(state.stick_y, stick_y_inverted());
    }
    if (gyro.has_value()) {
        state.stick_x = blend_gyro_steering(state.stick_x, gyro->x);
        state.stick_y = blend_gyro_steering(state.stick_y, gyro->y);
    }
#else
    (void)controller;
    (void)gyro_controller;
    (void)include_keyboard;
    (void)blocked;
#endif
    return state;
}
