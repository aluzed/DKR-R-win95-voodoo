#pragma once

#include <algorithm>
#include <cmath>

namespace dkr::runtime::input {

inline constexpr float kRadiansPerDegree = 0.01745329251994329577F;

constexpr float clamp_gyro_sensitivity(float percent) {
    return std::clamp(percent, 25.0F, 300.0F);
}

constexpr float clamp_gyro_deadzone(float degrees_per_second) {
    return std::clamp(degrees_per_second, 0.0F, 12.0F);
}

inline float gyro_steering_value(float radians_per_second, float bias,
                                 float deadzone_degrees_per_second,
                                 float sensitivity_percent, bool inverted) {
    float value = radians_per_second - bias;
    const float deadzone = clamp_gyro_deadzone(deadzone_degrees_per_second) *
                           kRadiansPerDegree;
    if (std::fabs(value) <= deadzone) {
        return 0.0F;
    }
    value = std::copysign(std::fabs(value) - deadzone, value);
    // At 100%, a deliberate 120-degree-per-second motion reaches full stick.
    constexpr float kFullSteerRate = 120.0F * kRadiansPerDegree;
    value = value * (clamp_gyro_sensitivity(sensitivity_percent) / 100.0F) /
            kFullSteerRate;
    if (inverted) {
        value = -value;
    }
    return std::clamp(value, -1.0F, 1.0F);
}

constexpr float blend_gyro_steering(float analogue, float gyro) {
    return std::clamp(analogue + gyro, -1.0F, 1.0F);
}

} // namespace dkr::runtime::input
