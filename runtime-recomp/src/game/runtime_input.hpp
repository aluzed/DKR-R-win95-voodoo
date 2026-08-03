#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct _SDL_GameController;
typedef struct _SDL_GameController SDL_GameController;

namespace dkr::runtime::input {

enum class Action : std::uint8_t {
    StickUp,
    StickDown,
    StickLeft,
    StickRight,
    A,
    B,
    Z,
    Start,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    L,
    R,
    CUp,
    CDown,
    CLeft,
    CRight,
    Count,
};

enum class GyroAxis : std::uint8_t {
    Roll = 0,
    Yaw = 1,
};

struct State {
    std::uint16_t buttons = 0;
    float stick_x = 0.0F;
    float stick_y = 0.0F;
};

constexpr int kUnbound = -1;

std::size_t action_count();
const char* action_identifier(Action action);
const char* action_label(Action action);

int keyboard_binding(Action action);
int controller_binding(Action action);
void set_keyboard_binding(Action action, int scancode);
void set_controller_binding(Action action, int source);
void reset_defaults();

float stick_deadzone();
void set_stick_deadzone(float percent);
float stick_anti_deadzone();
void set_stick_anti_deadzone(float percent);
float stick_sensitivity();
void set_stick_sensitivity(float percent);
float stick_curve();
void set_stick_curve(float exponent);
bool stick_x_inverted();
void set_stick_x_inverted(bool inverted);
bool stick_y_inverted();
void set_stick_y_inverted(bool inverted);
float trigger_threshold();
void set_trigger_threshold(float threshold);

bool gyro_enabled();
void set_gyro_enabled(bool enabled);
float gyro_sensitivity();
void set_gyro_sensitivity(float percent);
float gyro_deadzone();
void set_gyro_deadzone(float degrees_per_second);
bool gyro_inverted();
void set_gyro_inverted(bool inverted);
GyroAxis gyro_axis();
void set_gyro_axis(GyroAxis axis);
void begin_gyro_calibration();
bool gyro_calibrating();
float gyro_calibration_progress();

int encode_controller_button(int button);
int encode_controller_axis(int axis, bool positive);
std::string keyboard_binding_name(int scancode);
std::string controller_binding_name(int source);

State poll(SDL_GameController* controller, bool include_keyboard, bool blocked);

} // namespace dkr::runtime::input
