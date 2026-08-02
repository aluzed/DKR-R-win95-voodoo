#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace dkrport::input {

enum class Action : std::uint8_t {
    A,
    B,
    Z,
    Start,
    L,
    R,
    CUp,
    CDown,
    CLeft,
    CRight,
    DUp,
    DDown,
    DLeft,
    DRight,
    StickUp,
    StickDown,
    StickLeft,
    StickRight,
    Count
};

struct ActionBinding {
    std::string keyboard;
    std::string gamepad;
};

struct InputConfig {
    std::array<ActionBinding, static_cast<std::size_t>(Action::Count)> actions{};
    std::string analogueStick = "Left";
    float deadzone = 0.18F;
    bool invertX = false;
    bool invertY = false;
};

struct N64ControllerState {
    std::uint16_t buttons = 0;
    std::int8_t stickX = 0;
    std::int8_t stickY = 0;
};

constexpr std::uint16_t kButtonA = 0x8000U;
constexpr std::uint16_t kButtonB = 0x4000U;
constexpr std::uint16_t kButtonZ = 0x2000U;
constexpr std::uint16_t kButtonStart = 0x1000U;
constexpr std::uint16_t kButtonDUp = 0x0800U;
constexpr std::uint16_t kButtonDDown = 0x0400U;
constexpr std::uint16_t kButtonDLeft = 0x0200U;
constexpr std::uint16_t kButtonDRight = 0x0100U;
constexpr std::uint16_t kButtonL = 0x0020U;
constexpr std::uint16_t kButtonR = 0x0010U;
constexpr std::uint16_t kButtonCUp = 0x0008U;
constexpr std::uint16_t kButtonCDown = 0x0004U;
constexpr std::uint16_t kButtonCLeft = 0x0002U;
constexpr std::uint16_t kButtonCRight = 0x0001U;

InputConfig DefaultInputConfig();
const char* ActionId(Action action);
const char* ActionLabel(Action action);
std::uint16_t ActionButtonMask(Action action);
bool LoadInputConfig(const std::filesystem::path& path, InputConfig& config, std::string& error);
bool SaveInputConfig(const std::filesystem::path& path, const InputConfig& config, std::string& error);
std::string InputConfigJson(const InputConfig& config);

} // namespace dkrport::input
