#pragma once

#include "dkrport/core/Logger.h"
#include "dkrport/input/InputConfig.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dkrport::input {

enum class BindingSlot {
    Keyboard,
    Gamepad
};

struct CaptureResult {
    Action action = Action::A;
    BindingSlot slot = BindingSlot::Keyboard;
    bool completed = false;
    bool cancelled = false;
    std::string binding;
    std::string error;
};

class InputManager {
  public:
    InputManager(Logger& logger, std::filesystem::path configPath);
    ~InputManager();

    bool Initialise(std::string& error);
    void Shutdown();
    bool HandleEvent(const SDL_Event& event);
    [[nodiscard]] N64ControllerState SampleController() const;

    void BeginCapture(Action action, BindingSlot slot);
    void CancelCapture();
    [[nodiscard]] bool CaptureActive() const;
    std::optional<CaptureResult> TakeCaptureResult();

    bool ResetDefaults(std::string& error);
    bool SetDeadzone(float deadzone, std::string& error);
    bool ToggleInvertX(std::string& error);
    bool ToggleInvertY(std::string& error);
    bool ToggleAnalogueStick(std::string& error);

    [[nodiscard]] const InputConfig& Config() const;
    [[nodiscard]] std::string PrimaryGamepadName() const;
    [[nodiscard]] std::size_t ConnectedGamepadCount() const;

  private:
    bool Save(std::string& error);
    void OpenGamepad(SDL_JoystickID id);
    void CloseGamepad(SDL_JoystickID id);
    void CompleteCapture(const std::string& binding);
    bool KeyboardBindingPressed(const std::string& binding) const;
    bool GamepadBindingPressed(SDL_Gamepad* gamepad, const std::string& binding) const;
    SDL_Gamepad* PrimaryGamepad() const;

    Logger& m_logger;
    std::filesystem::path m_configPath;
    InputConfig m_config;
    std::map<SDL_JoystickID, SDL_Gamepad*> m_gamepads;
    bool m_initialised;
    bool m_captureActive;
    Action m_captureAction;
    BindingSlot m_captureSlot;
    std::uint64_t m_captureStarted;
    std::optional<CaptureResult> m_captureResult;
};

} // namespace dkrport::input
