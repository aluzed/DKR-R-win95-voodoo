/* E06-S02 - implementation. See joystick_map.h. */
#include "joystick_map.h"

#include <ctype.h>
#include <string.h>

static const unsigned short kDefaultButtons[DKR_JOY_MAX_BUTTONS] = {
    DKR_N64_A, DKR_N64_B, DKR_N64_Z, DKR_N64_START,
    DKR_N64_L, DKR_N64_R, DKR_N64_CDOWN, DKR_N64_CLEFT,
};

void dkr_joy_config_default(dkr_joy_config *config, unsigned x_min, unsigned x_max,
                            unsigned y_min, unsigned y_max)
{
    config->x_min = x_min; config->x_max = x_max;
    config->y_min = y_min; config->y_max = y_max;
    config->dead_zone_percent = 15;
    memcpy(config->button_map, kDefaultButtons, sizeof(kDefaultButtons));
}

static unsigned short button_named(const char *name, size_t length)
{
    static const struct { const char *name; unsigned short bit; } kNames[] = {
        {"A", DKR_N64_A}, {"B", DKR_N64_B}, {"Z", DKR_N64_Z},
        {"START", DKR_N64_START}, {"L", DKR_N64_L}, {"R", DKR_N64_R},
        {"CU", DKR_N64_CUP}, {"CD", DKR_N64_CDOWN},
        {"CL", DKR_N64_CLEFT}, {"CR", DKR_N64_CRIGHT},
        {"DU", DKR_N64_DUP}, {"DD", DKR_N64_DDOWN},
        {"DL", DKR_N64_DLEFT}, {"DR", DKR_N64_DRIGHT},
    };
    size_t i, k;
    for (i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        if (strlen(kNames[i].name) != length) { continue; }
        for (k = 0; k < length; k++) {
            if (toupper((unsigned char)name[k]) != kNames[i].name[k]) { break; }
        }
        if (k == length) { return kNames[i].bit; }
    }
    return 0;
}

int dkr_joy_parse_buttons(dkr_joy_config *config, const char *list)
{
    int index = 0, understood = 0;
    const char *p = list;
    if (!list) { return 0; }
    while (index < DKR_JOY_MAX_BUTTONS) {
        const char *start;
        size_t length;
        while (*p == ' ' || *p == '\t') { p++; }
        start = p;
        while (*p != '\0' && *p != ',') { p++; }
        length = (size_t)(p - start);
        while (length > 0 && (start[length - 1] == ' ' || start[length - 1] == '\t')) {
            length--;
        }
        if (length == 1 && start[0] == '-') {
            config->button_map[index] = 0;
            understood++;
        } else {
            config->button_map[index] = button_named(start, length);
            if (config->button_map[index] != 0) { understood++; }
        }
        index++;
        if (*p != ',') { break; }
        p++;
    }
    for (; index < DKR_JOY_MAX_BUTTONS; index++) { config->button_map[index] = 0; }
    return understood;
}

float dkr_joy_axis(unsigned value, unsigned min, unsigned max, int dead_zone_percent)
{
    float centre, half, v, dead;
    if (max <= min) { return 0.0f; }
    if (value < min) { value = min; }
    if (value > max) { value = max; }
    if (dead_zone_percent < 0) { dead_zone_percent = 0; }
    if (dead_zone_percent > 90) { dead_zone_percent = 90; }
    centre = ((float)min + (float)max) * 0.5f;
    half = ((float)max - (float)min) * 0.5f;
    v = ((float)value - centre) / half;            /* -1..1 */
    dead = (float)dead_zone_percent / 100.0f;
    if (v > -dead && v < dead) { return 0.0f; }
    /* Rescaled past the dead zone, so full deflection still reads 1. */
    v = (v > 0.0f) ? (v - dead) / (1.0f - dead) : (v + dead) / (1.0f - dead);
    if (v > 1.0f) { v = 1.0f; }
    if (v < -1.0f) { v = -1.0f; }
    return v;
}

void dkr_joy_map(const dkr_joy_config *config, const dkr_joy_reading *reading,
                 dkr_joy_n64 *out)
{
    int i;
    out->stick_x = dkr_joy_axis(reading->x, config->x_min, config->x_max,
                                config->dead_zone_percent);
    out->stick_y = -dkr_joy_axis(reading->y, config->y_min, config->y_max,
                                 config->dead_zone_percent);
    out->buttons = 0;
    for (i = 0; i < DKR_JOY_MAX_BUTTONS; i++) {
        if (reading->buttons & (1u << i)) { out->buttons |= config->button_map[i]; }
    }
    if (reading->pov >= 0) {
        /* Each direction covers 135 degrees centred on its own, so that the
           four 45-degree diagonals between them set two bits: 292.5..67.5 is
           up, 22.5..157.5 right, and so on. */
        const int d = reading->pov % 36000;
        if (d >= 29250 || d < 6750)  { out->buttons |= DKR_N64_DUP; }
        if (d >= 2250 && d < 15750)  { out->buttons |= DKR_N64_DRIGHT; }
        if (d >= 11250 && d < 24750) { out->buttons |= DKR_N64_DDOWN; }
        if (d >= 20250 && d < 33750) { out->buttons |= DKR_N64_DLEFT; }
    }
}
