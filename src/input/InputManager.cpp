#include "dkrport/input/InputManager.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace dkrport::input {
namespace {

constexpr Sint16 kDigitalAxisThreshold = 18000;
constexpr Sint16 kCaptureAxisThreshold = 24500;
constexpr float kN64StickRange = 80.0F;

const char* GamepadButtonName(SDL_GamepadButton button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return "South";
        case SDL_GAMEPAD_BUTTON_EAST: return "East";
        case SDL_GAMEPAD_BUTTON_WEST: return "West";
        case SDL_GAMEPAD_BUTTON_NORTH: return "North";
        case SDL_GAMEPAD_BUTTON_BACK: return "Back";
        case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
        case SDL_GAMEPAD_BUTTON_START: return "Start";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "Left Stick Click";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "Right Stick Click";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "Left Shoulder";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "Right Shoulder";
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad Up";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad Down";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad Left";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad Right";
        case SDL_GAMEPAD_BUTTON_MISC1: return "Misc 1";
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "Right Paddle 1";
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "Left Paddle 1";
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "Right Paddle 2";
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "Left Paddle 2";
        case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
        default: return nullptr;
    }
}

bool ButtonFromName(const std::string& name, SDL_GamepadButton& button) {
    for (int value = SDL_GAMEPAD_BUTTON_SOUTH; value < SDL_GAMEPAD_BUTTON_COUNT; ++value) {
        const auto candidate = static_cast<SDL_GamepadButton>(value);
        const char* candidateName = GamepadButtonName(candidate);
        if (candidateName && name == candidateName) {
            button = candidate;
            return true;
        }
    }
    return false;
}

std::string AxisDirectionName(SDL_GamepadAxis axis, Sint16 value) {
    const bool positive = value > 0;
    switch (axis) {
        case SDL_GAMEPAD_AXIS_LEFTX: return positive ? "Left Stick Right" : "Left Stick Left";
        case SDL_GAMEPAD_AXIS_LEFTY: return positive ? "Left Stick Down" : "Left Stick Up";
        case SDL_GAMEPAD_AXIS_RIGHTX: return positive ? "Right Stick Right" : "Right Stick Left";
        case SDL_GAMEPAD_AXIS_RIGHTY: return positive ? "Right Stick Down" : "Right Stick Up";
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return "Left Trigger";
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "Right Trigger";
        default: return {};
    }
}

bool AxisBindingPressed(SDL_Gamepad* gamepad, const std::string& name) {
    struct AxisBinding {
        const char* name;
        SDL_GamepadAxis axis;
        bool positive;
    };
    static constexpr AxisBinding bindings[] = {
        {"Left Stick Left", SDL_GAMEPAD_AXIS_LEFTX, false},
        {"Left Stick Right", SDL_GAMEPAD_AXIS_LEFTX, true},
        {"Left Stick Up", SDL_GAMEPAD_AXIS_LEFTY, false},
        {"Left Stick Down", SDL_GAMEPAD_AXIS_LEFTY, true},
        {"Right Stick Left", SDL_GAMEPAD_AXIS_RIGHTX, false},
        {"Right Stick Right", SDL_GAMEPAD_AXIS_RIGHTX, true},
        {"Right Stick Up", SDL_GAMEPAD_AXIS_RIGHTY, false},
        {"Right Stick Down", SDL_GAMEPAD_AXIS_RIGHTY, true},
        {"Left Trigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true},
        {"Right Trigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, true},
    };
    for (const auto& binding : bindings) {
        if (name != binding.name) continue;
        const Sint16 value = SDL_GetGamepadAxis(gamepad, binding.axis);
        return binding.positive ? value >= kDigitalAxisThreshold : value <= -kDigitalAxisThreshold;
    }
    return false;
}

float NormaliseAxis(Sint16 value) {
    if (value >= 0) return static_cast<float>(value) / 32767.0F;
    return static_cast<float>(value) / 32768.0F;
}

std::int8_t ToN64Axis(float value) {
    const float clamped = std::clamp(value, -1.0F, 1.0F);
    const long rounded = std::lround(clamped * kN64StickRange);
    return static_cast<std::int8_t>(std::clamp(rounded, -80L, 80L));
}

} // namespace

InputManager::InputManager(Logger& logger, std::filesystem::path configPath)
    : m_logger(logger), m_configPath(std::move(configPath)), m_config(DefaultInputConfig()), m_initialised(false),
      m_captureActive(false), m_captureAction(Action::A), m_captureSlot(BindingSlot::Keyboard), m_captureStarted(0U) {}

InputManager::~InputManager() {
    Shutdown();
}

bool InputManager::Initialise(std::string& error) {
    if (m_initialised) return true;
    std::string loadError;
    if (!LoadInputConfig(m_configPath, m_config, loadError)) {
        m_logger.Warning("Input configuration could not be read and was reset to defaults: " + loadError);
        m_config = DefaultInputConfig();
        std::string saveError;
        if (!SaveInputConfig(m_configPath, m_config, saveError)) {
            error = "The control profile was invalid and a default replacement could not be saved: " + saveError;
            return false;
        }
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int index = 0; index < count; ++index) OpenGamepad(ids[index]);
        SDL_free(ids);
    }
    m_initialised = true;
    m_logger.Info("Input configuration loaded from " + m_configPath.string());
    return true;
}

void InputManager::Shutdown() {
    for (auto& [id, gamepad] : m_gamepads) {
        static_cast<void>(id);
        if (gamepad) SDL_CloseGamepad(gamepad);
    }
    m_gamepads.clear();
    m_initialised = false;
    m_captureActive = false;
}

bool InputManager::HandleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        OpenGamepad(event.gdevice.which);
        return false;
    }
    if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        CloseGamepad(event.gdevice.which);
        return false;
    }

    if (!m_captureActive) return false;

    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_ESCAPE) {
            CancelCapture();
            return true;
        }
        if (event.key.key == SDLK_BACKSPACE || event.key.key == SDLK_DELETE) {
            CompleteCapture("Unbound");
            return true;
        }
        if (m_captureSlot != BindingSlot::Keyboard || event.key.repeat) return true;
        const char* name = SDL_GetScancodeName(event.key.scancode);
        if (name && *name) CompleteCapture(name);
        return true;
    }

    if (m_captureSlot != BindingSlot::Gamepad) return false;
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const char* name = GamepadButtonName(static_cast<SDL_GamepadButton>(event.gbutton.button));
        if (name) CompleteCapture(name);
        return true;
    }
    if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && SDL_GetTicks() - m_captureStarted >= 250U) {
        if (std::abs(static_cast<int>(event.gaxis.value)) < kCaptureAxisThreshold) return true;
        const std::string name = AxisDirectionName(static_cast<SDL_GamepadAxis>(event.gaxis.axis), event.gaxis.value);
        if (!name.empty()) CompleteCapture(name);
        return true;
    }
    return false;
}

N64ControllerState InputManager::SampleController() const {
    N64ControllerState output;
    SDL_Gamepad* gamepad = PrimaryGamepad();

    constexpr std::size_t buttonActionCount = static_cast<std::size_t>(Action::StickUp);
    for (std::size_t index = 0; index < buttonActionCount; ++index) {
        const auto action = static_cast<Action>(index);
        const ActionBinding& binding = m_config.actions[index];
        const bool pressed = KeyboardBindingPressed(binding.keyboard) ||
                             (gamepad && GamepadBindingPressed(gamepad, binding.gamepad));
        if (pressed) output.buttons |= ActionButtonMask(action);
    }

    float x = 0.0F;
    float y = 0.0F;
    if (gamepad) {
        const bool right = m_config.analogueStick == "Right";
        const SDL_GamepadAxis axisX = right ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX;
        const SDL_GamepadAxis axisY = right ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY;
        x = NormaliseAxis(SDL_GetGamepadAxis(gamepad, axisX));
        y = NormaliseAxis(SDL_GetGamepadAxis(gamepad, axisY));
        const float magnitude = std::sqrt(x * x + y * y);
        if (magnitude <= m_config.deadzone) {
            x = 0.0F;
            y = 0.0F;
        } else if (magnitude > 0.0F) {
            const float scaledMagnitude = std::clamp((magnitude - m_config.deadzone) / (1.0F - m_config.deadzone), 0.0F, 1.0F);
            const float scale = scaledMagnitude / magnitude;
            x *= scale;
            y *= scale;
        }
    }

    const auto key = [this](Action action) {
        return KeyboardBindingPressed(m_config.actions[static_cast<std::size_t>(action)].keyboard);
    };
    const bool left = key(Action::StickLeft);
    const bool right = key(Action::StickRight);
    const bool up = key(Action::StickUp);
    const bool down = key(Action::StickDown);
    if (left != right) x = left ? -1.0F : 1.0F;
    if (up != down) y = up ? -1.0F : 1.0F;

    if (m_config.invertX) x = -x;
    if (m_config.invertY) y = -y;
    output.stickX = ToN64Axis(x);
    output.stickY = ToN64Axis(-y);
    return output;
}

void InputManager::BeginCapture(Action action, BindingSlot slot) {
    m_captureActive = true;
    m_captureAction = action;
    m_captureSlot = slot;
    m_captureStarted = SDL_GetTicks();
    m_captureResult.reset();
}

void InputManager::CancelCapture() {
    if (!m_captureActive) return;
    CaptureResult result;
    result.action = m_captureAction;
    result.slot = m_captureSlot;
    result.cancelled = true;
    m_captureResult = result;
    m_captureActive = false;
}

bool InputManager::CaptureActive() const {
    return m_captureActive;
}

std::optional<CaptureResult> InputManager::TakeCaptureResult() {
    std::optional<CaptureResult> result = std::move(m_captureResult);
    m_captureResult.reset();
    return result;
}

bool InputManager::ResetDefaults(std::string& error) {
    m_config = DefaultInputConfig();
    return Save(error);
}

bool InputManager::SetDeadzone(float deadzone, std::string& error) {
    m_config.deadzone = std::clamp(deadzone, 0.0F, 0.60F);
    return Save(error);
}

bool InputManager::ToggleInvertX(std::string& error) {
    m_config.invertX = !m_config.invertX;
    return Save(error);
}

bool InputManager::ToggleInvertY(std::string& error) {
    m_config.invertY = !m_config.invertY;
    return Save(error);
}

bool InputManager::ToggleAnalogueStick(std::string& error) {
    m_config.analogueStick = m_config.analogueStick == "Left" ? "Right" : "Left";
    return Save(error);
}

const InputConfig& InputManager::Config() const {
    return m_config;
}

std::string InputManager::PrimaryGamepadName() const {
    SDL_Gamepad* gamepad = PrimaryGamepad();
    if (!gamepad) return "No controller connected";
    const char* name = SDL_GetGamepadName(gamepad);
    return name && *name ? name : "Connected controller";
}

std::size_t InputManager::ConnectedGamepadCount() const {
    return m_gamepads.size();
}

bool InputManager::Save(std::string& error) {
    if (!SaveInputConfig(m_configPath, m_config, error)) return false;
    m_logger.Info("Input configuration saved.");
    return true;
}

void InputManager::OpenGamepad(SDL_JoystickID id) {
    if (m_gamepads.find(id) != m_gamepads.end()) return;
    SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
    if (!gamepad) {
        m_logger.Warning(std::string("Could not open controller: ") + SDL_GetError());
        return;
    }
    m_gamepads[SDL_GetGamepadID(gamepad)] = gamepad;
    const char* name = SDL_GetGamepadName(gamepad);
    m_logger.Info(std::string("Controller connected: ") + (name && *name ? name : "unknown"));
}

void InputManager::CloseGamepad(SDL_JoystickID id) {
    const auto found = m_gamepads.find(id);
    if (found == m_gamepads.end()) return;
    SDL_CloseGamepad(found->second);
    m_gamepads.erase(found);
    m_logger.Info("Controller disconnected.");
}

void InputManager::CompleteCapture(const std::string& binding) {
    ActionBinding& actionBinding = m_config.actions[static_cast<std::size_t>(m_captureAction)];
    if (m_captureSlot == BindingSlot::Keyboard) actionBinding.keyboard = binding;
    else actionBinding.gamepad = binding;

    CaptureResult result;
    result.action = m_captureAction;
    result.slot = m_captureSlot;
    result.binding = binding;
    std::string error;
    if (!Save(error)) result.error = error;
    else result.completed = true;
    m_captureResult = result;
    m_captureActive = false;
}

bool InputManager::KeyboardBindingPressed(const std::string& binding) const {
    if (binding.empty() || binding == "Unbound") return false;
    const SDL_Scancode scancode = SDL_GetScancodeFromName(binding.c_str());
    if (scancode == SDL_SCANCODE_UNKNOWN) return false;
    int count = 0;
    const bool* keyboard = SDL_GetKeyboardState(&count);
    return keyboard && static_cast<int>(scancode) >= 0 && static_cast<int>(scancode) < count && keyboard[scancode];
}

bool InputManager::GamepadBindingPressed(SDL_Gamepad* gamepad, const std::string& binding) const {
    if (!gamepad || binding.empty() || binding == "Unbound") return false;
    SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
    if (ButtonFromName(binding, button)) return SDL_GetGamepadButton(gamepad, button);
    return AxisBindingPressed(gamepad, binding);
}

SDL_Gamepad* InputManager::PrimaryGamepad() const {
    if (m_gamepads.empty()) return nullptr;
    return m_gamepads.begin()->second;
}

} // namespace dkrport::input
