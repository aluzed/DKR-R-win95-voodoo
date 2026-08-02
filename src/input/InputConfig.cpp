#include "dkrport/input/InputConfig.h"

#include "dkrport/core/FileUtil.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

namespace dkrport::input {
namespace {

constexpr std::array<Action, static_cast<std::size_t>(Action::Count)> kActions = {
    Action::A, Action::B, Action::Z, Action::Start, Action::L, Action::R,
    Action::CUp, Action::CDown, Action::CLeft, Action::CRight,
    Action::DUp, Action::DDown, Action::DLeft, Action::DRight,
    Action::StickUp, Action::StickDown, Action::StickLeft, Action::StickRight
};

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyPosition = json.find(needle);
    if (keyPosition == std::string::npos) return {};
    const std::size_t colon = json.find(':', keyPosition + needle.size());
    if (colon == std::string::npos) return {};
    std::size_t position = colon + 1U;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])) != 0) ++position;
    if (position >= json.size() || json[position] != '"') return {};
    ++position;

    std::string output;
    bool escaped = false;
    for (; position < json.size(); ++position) {
        const char character = json[position];
        if (escaped) {
            switch (character) {
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case '\\': output.push_back('\\'); break;
                case '"': output.push_back('"'); break;
                default: output.push_back(character); break;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return output;
        } else {
            output.push_back(character);
        }
    }
    return {};
}

bool ExtractJsonBool(const std::string& json, const std::string& key, bool fallback) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyPosition = json.find(needle);
    if (keyPosition == std::string::npos) return fallback;
    const std::size_t colon = json.find(':', keyPosition + needle.size());
    if (colon == std::string::npos) return fallback;
    std::size_t position = colon + 1U;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])) != 0) ++position;
    if (json.compare(position, 4U, "true") == 0) return true;
    if (json.compare(position, 5U, "false") == 0) return false;
    return fallback;
}

float ExtractJsonFloat(const std::string& json, const std::string& key, float fallback) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyPosition = json.find(needle);
    if (keyPosition == std::string::npos) return fallback;
    const std::size_t colon = json.find(':', keyPosition + needle.size());
    if (colon == std::string::npos) return fallback;
    std::size_t position = colon + 1U;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])) != 0) ++position;
    std::size_t end = position;
    while (end < json.size()) {
        const char character = json[end];
        if (!(std::isdigit(static_cast<unsigned char>(character)) != 0 || character == '.' || character == '-' || character == '+')) break;
        ++end;
    }
    if (end == position) return fallback;
    try {
        return std::stof(json.substr(position, end - position));
    } catch (...) {
        return fallback;
    }
}

std::string BindingKey(Action action, const char* suffix) {
    return std::string(ActionId(action)) + suffix;
}

} // namespace

InputConfig DefaultInputConfig() {
    InputConfig config;
    auto set = [&config](Action action, const char* keyboard, const char* gamepad) {
        config.actions[static_cast<std::size_t>(action)] = {keyboard, gamepad};
    };
    set(Action::A, "Space", "South");
    set(Action::B, "Left Ctrl", "West");
    set(Action::Z, "Left Shift", "Left Trigger");
    set(Action::Start, "Return", "Start");
    set(Action::L, "Q", "Left Shoulder");
    set(Action::R, "E", "Right Shoulder");
    set(Action::CUp, "I", "Right Stick Up");
    set(Action::CDown, "K", "Right Stick Down");
    set(Action::CLeft, "J", "Right Stick Left");
    set(Action::CRight, "L", "Right Stick Right");
    set(Action::DUp, "Up", "D-pad Up");
    set(Action::DDown, "Down", "D-pad Down");
    set(Action::DLeft, "Left", "D-pad Left");
    set(Action::DRight, "Right", "D-pad Right");
    set(Action::StickUp, "W", "Left Stick Up");
    set(Action::StickDown, "S", "Left Stick Down");
    set(Action::StickLeft, "A", "Left Stick Left");
    set(Action::StickRight, "D", "Left Stick Right");
    config.analogueStick = "Left";
    config.deadzone = 0.18F;
    return config;
}

const char* ActionId(Action action) {
    switch (action) {
        case Action::A: return "a";
        case Action::B: return "b";
        case Action::Z: return "z";
        case Action::Start: return "start";
        case Action::L: return "l";
        case Action::R: return "r";
        case Action::CUp: return "c_up";
        case Action::CDown: return "c_down";
        case Action::CLeft: return "c_left";
        case Action::CRight: return "c_right";
        case Action::DUp: return "d_up";
        case Action::DDown: return "d_down";
        case Action::DLeft: return "d_left";
        case Action::DRight: return "d_right";
        case Action::StickUp: return "stick_up";
        case Action::StickDown: return "stick_down";
        case Action::StickLeft: return "stick_left";
        case Action::StickRight: return "stick_right";
        case Action::Count: break;
    }
    return "unknown";
}

const char* ActionLabel(Action action) {
    switch (action) {
        case Action::A: return "A BUTTON";
        case Action::B: return "B BUTTON";
        case Action::Z: return "Z TRIGGER";
        case Action::Start: return "START";
        case Action::L: return "L TRIGGER";
        case Action::R: return "R TRIGGER";
        case Action::CUp: return "C UP";
        case Action::CDown: return "C DOWN";
        case Action::CLeft: return "C LEFT";
        case Action::CRight: return "C RIGHT";
        case Action::DUp: return "D-PAD UP";
        case Action::DDown: return "D-PAD DOWN";
        case Action::DLeft: return "D-PAD LEFT";
        case Action::DRight: return "D-PAD RIGHT";
        case Action::StickUp: return "ANALOGUE UP";
        case Action::StickDown: return "ANALOGUE DOWN";
        case Action::StickLeft: return "ANALOGUE LEFT";
        case Action::StickRight: return "ANALOGUE RIGHT";
        case Action::Count: break;
    }
    return "UNKNOWN";
}

std::uint16_t ActionButtonMask(Action action) {
    switch (action) {
        case Action::A: return kButtonA;
        case Action::B: return kButtonB;
        case Action::Z: return kButtonZ;
        case Action::Start: return kButtonStart;
        case Action::L: return kButtonL;
        case Action::R: return kButtonR;
        case Action::CUp: return kButtonCUp;
        case Action::CDown: return kButtonCDown;
        case Action::CLeft: return kButtonCLeft;
        case Action::CRight: return kButtonCRight;
        case Action::DUp: return kButtonDUp;
        case Action::DDown: return kButtonDDown;
        case Action::DLeft: return kButtonDLeft;
        case Action::DRight: return kButtonDRight;
        case Action::StickUp:
        case Action::StickDown:
        case Action::StickLeft:
        case Action::StickRight: return 0U;
        case Action::Count: break;
    }
    return 0U;
}

std::string InputConfigJson(const InputConfig& config) {
    std::ostringstream json;
    json << "{\n  \"schemaVersion\": 1,\n";
    json << "  \"analogueStick\": \"" << JsonEscape(config.analogueStick) << "\",\n";
    json << "  \"deadzone\": " << std::clamp(config.deadzone, 0.0F, 0.95F) << ",\n";
    json << "  \"invertX\": " << (config.invertX ? "true" : "false") << ",\n";
    json << "  \"invertY\": " << (config.invertY ? "true" : "false") << ",\n";
    json << "  \"bindings\": {\n";
    for (std::size_t index = 0; index < kActions.size(); ++index) {
        const Action action = kActions[index];
        const ActionBinding& binding = config.actions[static_cast<std::size_t>(action)];
        json << "    \"" << ActionId(action) << "_keyboard\": \"" << JsonEscape(binding.keyboard) << "\",\n";
        json << "    \"" << ActionId(action) << "_gamepad\": \"" << JsonEscape(binding.gamepad) << "\"";
        if (index + 1U != kActions.size()) json << ',';
        json << '\n';
    }
    json << "  }\n}\n";
    return json.str();
}

bool SaveInputConfig(const std::filesystem::path& path, const InputConfig& config, std::string& error) {
    const std::string json = InputConfigJson(config);
    return WriteTextFileAtomic(path, json, error);
}

bool LoadInputConfig(const std::filesystem::path& path, InputConfig& config, std::string& error) {
    config = DefaultInputConfig();
    if (!std::filesystem::exists(path)) return true;

    std::string json;
    if (!ReadTextFile(path, json, error, 1024U * 1024U)) return false;
    const std::string analogueStick = ExtractJsonString(json, "analogueStick");
    if (analogueStick == "Left" || analogueStick == "Right") config.analogueStick = analogueStick;
    config.deadzone = std::clamp(ExtractJsonFloat(json, "deadzone", config.deadzone), 0.0F, 0.95F);
    config.invertX = ExtractJsonBool(json, "invertX", config.invertX);
    config.invertY = ExtractJsonBool(json, "invertY", config.invertY);

    for (const Action action : kActions) {
        ActionBinding& binding = config.actions[static_cast<std::size_t>(action)];
        const std::string keyboard = ExtractJsonString(json, BindingKey(action, "_keyboard"));
        const std::string gamepad = ExtractJsonString(json, BindingKey(action, "_gamepad"));
        if (!keyboard.empty()) binding.keyboard = keyboard;
        if (!gamepad.empty()) binding.gamepad = gamepad;
    }
    return true;
}

} // namespace dkrport::input
