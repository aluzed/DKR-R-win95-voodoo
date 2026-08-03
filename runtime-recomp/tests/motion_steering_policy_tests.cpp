#include "motion_steering_policy.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace dkr::runtime::input;

int main() {
    static_assert(clamp_gyro_sensitivity(0.0F) == 25.0F);
    static_assert(clamp_gyro_sensitivity(100.0F) == 100.0F);
    static_assert(clamp_gyro_sensitivity(500.0F) == 300.0F);
    static_assert(clamp_gyro_deadzone(-1.0F) == 0.0F);
    static_assert(clamp_gyro_deadzone(20.0F) == 12.0F);
    static_assert(blend_gyro_steering(0.75F, 0.75F) == 1.0F);
    static_assert(blend_gyro_steering(-0.75F, -0.75F) == -1.0F);

    assert(gyro_steering_value(0.01F, 0.01F, 2.0F, 100.0F, false) == 0.0F);
    const float right = gyro_steering_value(120.0F * kRadiansPerDegree,
                                            0.0F, 0.0F, 100.0F, false);
    assert(std::fabs(right - 1.0F) < 0.0001F);
    const float left = gyro_steering_value(120.0F * kRadiansPerDegree,
                                           0.0F, 0.0F, 100.0F, true);
    assert(std::fabs(left + 1.0F) < 0.0001F);
    std::puts("[test][motion-steering-policy] PASS");
    return 0;
}
